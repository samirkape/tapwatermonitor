package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"github.com/gin-gonic/gin"
	"github.com/joho/godotenv"
	"github.com/stretchr/testify/assert"
	"github.com/supabase-community/supabase-go"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"
)

type testEnv struct {
	router         *gin.Engine
	supabaseClient *supabase.Client
	cleanup        func()
}

func setupTestEnvironment(t *testing.T) *testEnv {
	// Load test environment variables
	err := godotenv.Load("test.env")
	if err != nil {
		t.Fatalf("Error loading test.env file: %v", err)
	}

	// Set test mode for Gin
	gin.SetMode(gin.TestMode)

	// Initialize router
	router := gin.Default()
	router.POST("/default/tapwater", TapWaterStatusHandler)
	router.GET("/default/tapwater/start", TapWaterStartGetHandler)

	// Initialize Supabase client
	supabaseClient, err := createSupabaseClient()
	if err != nil {
		t.Fatalf("Failed to create Supabase client: %v", err)
	}

	// Set test environment variables
	os.Setenv("ENABLE_SMS", "false") // Disable SMS during tests
	os.Setenv("DEBUG", "true")

	return &testEnv{
		router:         router,
		supabaseClient: supabaseClient,
		cleanup: func() {
			// Cleanup function to be called after tests
			cleanupTestData(t, supabaseClient)
		},
	}
}

func cleanupTestData(t *testing.T, client *supabase.Client) {
	// Clean up test data from both tables
	_, _, err := client.From("tapwaterdb").Delete("", "").Execute()
	if err != nil {
		t.Logf("Error cleaning up tapwaterdb: %v", err)
	}

	_, _, err = client.From("start_time").Delete("", "").Execute()
	if err != nil {
		t.Logf("Error cleaning up start_time: %v", err)
	}
}

// Helper function to make HTTP requests
func makeRequest(router *gin.Engine, method, url string, body interface{}) *httptest.ResponseRecorder {
	var reqBody []byte
	var err error
	if body != nil {
		reqBody, err = json.Marshal(body)
		if err != nil {
			panic(fmt.Sprintf("Failed to marshal request body: %v", err))
		}
	}

	w := httptest.NewRecorder()
	req, _ := http.NewRequest(method, url, bytes.NewBuffer(reqBody))
	req.Header.Set("Content-Type", "application/json")
	router.ServeHTTP(w, req)
	return w
}

// Helper function to verify database records
func verifyDatabaseRecord(t *testing.T, client *supabase.Client, tableName string, filter map[string]string, expectedFields map[string]interface{}) {
	var result map[string]interface{}
	query := client.From(tableName).Select("*", "", false)

	// Apply filters
	for key, value := range filter {
		query = query.Eq(key, value)
	}

	_, err := query.Single().ExecuteTo(&result)
	assert.NoError(t, err)

	// Verify expected fields
	for key, expected := range expectedFields {
		assert.Equal(t, expected, result[key], fmt.Sprintf("Field %s mismatch", key))
	}
}

func TestWaterStatusIntegration(t *testing.T) {
	env := setupTestEnvironment(t)
	defer env.cleanup()

	location, _ := time.LoadLocation("Asia/Kolkata")
	currentTime := time.Now().In(location)
	testDate := currentTime.Format("02-01-2006")

	t.Run("Complete water monitoring flow", func(t *testing.T) {
		// Test start status
		startStatus := waterStatus{
			Status: "start",
			Date:   testDate,
		}

		resp := makeRequest(env.router, "POST", "/default/tapwater", startStatus)
		assert.Equal(t, http.StatusOK, resp.Code)

		// Verify start_time record
		startTimeFields := map[string]interface{}{
			"date":       testDate,
			"start_time": currentTime.Format("15:04"),
		}
		verifyDatabaseRecord(t, env.supabaseClient, "start_time",
			map[string]string{"date": testDate}, startTimeFields)

		// Verify initial tapwater record
		tapwaterFields := map[string]interface{}{
			"date":       fmt.Sprintf("%s, %s", currentTime.Format("Mon"), testDate),
			"start_time": currentTime.Format("3:04 PM"),
		}
		verifyDatabaseRecord(t, env.supabaseClient, "tapwaterdb",
			map[string]string{"date": fmt.Sprintf("%s, %s", currentTime.Format("Mon"), testDate)},
			tapwaterFields)

		// Simulate time passing (15 minutes)
		time.Sleep(1 * time.Second) // Just for test demonstration

		// Test end status
		endStatus := waterStatus{
			Status: "end",
			Date:   testDate,
		}

		resp = makeRequest(env.router, "POST", "/default/tapwater", endStatus)
		assert.Equal(t, http.StatusOK, resp.Code)

		// Verify final tapwater record
		endTime := time.Now().In(location)
		duration := int(endTime.Sub(currentTime).Minutes())

		finalFields := map[string]interface{}{
			"date":       fmt.Sprintf("%s, %s", currentTime.Format("Mon"), testDate),
			"start_time": currentTime.Format("3:04 PM"),
			"end_time":   endTime.Format("3:04 PM"),
			"duration":   duration,
		}
		verifyDatabaseRecord(t, env.supabaseClient, "tapwaterdb",
			map[string]string{"date": fmt.Sprintf("%s, %s", currentTime.Format("Mon"), testDate)},
			finalFields)

		// Verify start_time record was deleted
		var startTimeRecord tapWaterStartTime
		_, err := env.supabaseClient.From("start_time").
			Select("*", "", false).
			Eq("date", testDate).
			Single().
			ExecuteTo(&startTimeRecord)
		assert.Error(t, err, "Start time record should be deleted")
	})

	t.Run("Invalid status handling", func(t *testing.T) {
		invalidStatus := waterStatus{
			Status: "invalid",
			Date:   testDate,
		}

		resp := makeRequest(env.router, "POST", "/default/tapwater", invalidStatus)
		assert.Equal(t, http.StatusBadRequest, resp.Code)
	})

	t.Run("Missing date handling", func(t *testing.T) {
		invalidRequest := waterStatus{
			Status: "start",
		}

		resp := makeRequest(env.router, "POST", "/default/tapwater", invalidRequest)
		assert.Equal(t, http.StatusBadRequest, resp.Code)
	})

	t.Run("Get start time", func(t *testing.T) {
		// First create a start time record
		startStatus := waterStatus{
			Status: "start",
			Date:   testDate,
		}
		resp := makeRequest(env.router, "POST", "/default/tapwater", startStatus)
		assert.Equal(t, http.StatusOK, resp.Code)

		// Then try to retrieve it
		resp = makeRequest(env.router, "GET", fmt.Sprintf("/default/tapwater/start?date=%s", testDate), nil)
		assert.Equal(t, http.StatusOK, resp.Code)

		var result tapWaterStartTime
		err := json.Unmarshal(resp.Body.Bytes(), &result)
		assert.NoError(t, err)
		assert.Equal(t, testDate, result.Date)
		assert.NotEmpty(t, result.StartTime)
	})
}

func TestErrorConditions(t *testing.T) {
	env := setupTestEnvironment(t)
	defer env.cleanup()

	t.Run("Handle database connection failure", func(t *testing.T) {
		// Temporarily modify Supabase URL to trigger connection error
		originalURL := os.Getenv("SUPABASE_URL")
		os.Setenv("SUPABASE_URL", "invalid-url")
		defer os.Setenv("SUPABASE_URL", originalURL)

		status := waterStatus{
			Status: "start",
			Date:   time.Now().Format("02-01-2006"),
		}

		resp := makeRequest(env.router, "POST", "/default/tapwater", status)
		assert.Equal(t, http.StatusInternalServerError, resp.Code)
	})

	t.Run("Handle malformed JSON", func(t *testing.T) {
		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/default/tapwater", bytes.NewBuffer([]byte(`{"status": "start"`))) // Malformed JSON
		req.Header.Set("Content-Type", "application/json")
		env.router.ServeHTTP(w, req)
		assert.Equal(t, http.StatusBadRequest, w.Code)
	})
}
