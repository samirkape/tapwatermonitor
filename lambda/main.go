package main

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/aws/aws-lambda-go/events"
	"github.com/aws/aws-lambda-go/lambda"
	ginadapter "github.com/awslabs/aws-lambda-go-api-proxy/gin"
	"github.com/gin-gonic/gin"
	"github.com/joho/godotenv"
	"github.com/supabase-community/supabase-go"
	"github.com/twilio/twilio-go"
	twilioApi "github.com/twilio/twilio-go/rest/api/v2010"
	"log"
	"net/http"
	"os"
	"time"
)

// New status-based request structure
type waterStatus struct {
	Status string `json:"status"`
	Date   string `json:"date"`
}

// Existing structures for database
type tapWater struct {
	Date      string `json:"date,omitempty"`
	StartTime string `json:"start_time,omitempty"`
	EndTime   string `json:"end_time,omitempty"`
	Duration  int    `json:"duration,omitempty"`
}

type tapWaterStartTime struct {
	Date      string `json:"date,omitempty"`
	StartTime string `json:"start_time,omitempty"`
}

var ginLambda *ginadapter.GinLambda

const ACCOUNT_SID_ENV = "TWILIO_ACCOUNT_SID"
const ACCOUNT_AUTH_TOKEN_ENV = "TWILIO_AUTH_TOKEN"

var debug string
var enableSMS string

func Handler(ctx context.Context, req events.APIGatewayProxyRequest) (events.APIGatewayProxyResponse, error) {
	log.Printf("Processing Lambda request %s\n", req.RequestContext.RequestID)
	return ginLambda.ProxyWithContext(ctx, req)
}

func init() {
	err := godotenv.Load()
	if err != nil {
		log.Printf("error loading .env file")
	}
	debug = os.Getenv("DEBUG")
	enableSMS = os.Getenv("ENABLE_SMS")
}

func main() {
	r := gin.Default()
	r.POST("/default/tapwater", TapWaterStatusHandler) // Modified to handle status updates
	r.GET("/default/tapwater/start", TapWaterStartGetHandler)

	if debug == "true" {
		if err := r.Run("localhost:8080"); err != nil {
			panic("Failed to start server")
		}
	} else {
		ginLambda = ginadapter.New(r)
		lambda.Start(Handler)
	}
}

func createSupabaseClient() (*supabase.Client, error) {
	supabaseUrl := os.Getenv("SUPABASE_URL")
	supabaseKey := os.Getenv("SUPABASE_KEY")
	return supabase.NewClient(supabaseUrl, supabaseKey, nil)
}

// New function to handle status updates
func TapWaterStatusHandler(ctx *gin.Context) {
	const tableName = "tapwaterdb"
	var statusUpdate waterStatus

	if err := ctx.BindJSON(&statusUpdate); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	log.Printf("request: %+v, request body: %+v", ctx.FullPath(), statusUpdate)

	supabaseClient, err := createSupabaseClient()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"cannot connect to Supabase": err.Error()})
		return
	}

	location, _ := time.LoadLocation("Asia/Kolkata")
	currentTime := time.Now().In(location)

	switch statusUpdate.Status {
	case "start":
		startRecord := tapWaterStartTime{
			Date:      statusUpdate.Date,
			StartTime: currentTime.Format("15:04"), // 24-hour format
		}

		_, _, err = supabaseClient.From("start_time").Insert(startRecord, false, "", "", "").Execute()
		if err != nil {
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}

		initialRecord := tapWater{
			Date:      fmt.Sprintf("%s, %s", currentTime.Format("Mon"), statusUpdate.Date),
			StartTime: currentTime.Format("3:04 PM"),
		}

		_, _, err = supabaseClient.From(tableName).Insert(initialRecord, false, "", "", "").Execute()
		if err != nil {
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}

		sendArrivalSMS()

	case "end":
		var startTimeRecord tapWaterStartTime
		_, err = supabaseClient.From("start_time").
			Select("*", "", false).
			Eq("date", statusUpdate.Date).
			Single().
			ExecuteTo(&startTimeRecord)

		if err != nil {
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to retrieve start time"})
			return
		}

		startTimeStr := startTimeRecord.StartTime
		startTimeParsed, _ := time.Parse("15:04", startTimeStr)
		duration := int(currentTime.Sub(startTimeParsed).Minutes())

		record := tapWater{
			Date:      fmt.Sprintf("%s, %s", currentTime.Format("Mon"), statusUpdate.Date),
			StartTime: startTimeParsed.Format("3:04 PM"),
			EndTime:   currentTime.Format("3:04 PM"),
			Duration:  duration,
		}

		filter := map[string]string{
			"date":       record.Date,
			"start_time": record.StartTime,
		}

		// Update the record
		_, _, err = supabaseClient.From(tableName).
			Upsert(record, "", "", "").
			Match(filter).
			Execute()

		if err != nil {
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}

		sendDepartureSMS(record)

		// Clean up start time record
		_, _, err = supabaseClient.From("start_time").
			Delete("", "").
			Eq("date", statusUpdate.Date).
			Execute()

		if err != nil {
			log.Printf("Error deleting start time record: %v", err)
		}
	}

	ctx.JSON(http.StatusOK, gin.H{"message": "Status processed successfully"})
}

func TapWaterStartGetHandler(ctx *gin.Context) {
	const tableName = "start_time"

	var record tapWaterStartTime
	date := ctx.Query("date")

	supabaseClient, err := createSupabaseClient()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"cannot connect to Supabase": err.Error()})
		return
	}

	_, err = supabaseClient.From(tableName).
		Select("*", "", false).
		Eq("date", date).
		Single().
		ExecuteTo(&record)

	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	ctx.JSON(http.StatusOK, record)
}

func sendArrivalSMS() {
	location, _ := time.LoadLocation("Asia/Kolkata")
	timeInIST := time.Now().In(location)

	message := "\nWater has arrived on " + timeInIST.Format("03:04:05 PM")
	err := sendSMS(message)
	if err != nil {
		log.Printf("error sending arrival sms: %v", err)
	}
}

func sendDepartureSMS(record tapWater) {
	duration := fmt.Sprint(record.Duration)
	message := "\nDate: " + record.Date +
		"\nStart Time: " + record.StartTime +
		"\nEnd Time: " + record.EndTime +
		"\nDuration: " + duration + " minutes"

	if record.Duration != 0 {
		err := sendSMS(message)
		if err != nil {
			log.Printf("error sending departure sms: %v", err)
		}
	}
}

func sendSMS(message string) error {
	if enableSMS == "true" {
		accountSid := os.Getenv(ACCOUNT_SID_ENV)
		authToken := os.Getenv(ACCOUNT_AUTH_TOKEN_ENV)
		if accountSid == "" || authToken == "" {
			log.Fatal("twilio account SID or auth token not found")
		}

		client := twilio.NewRestClientWithParams(twilio.ClientParams{
			Username: accountSid,
			Password: authToken,
		})

		fromNumber := "+12512378296"
		toNumbers := []string{
			os.Getenv("MOBILE_NO1"),
			os.Getenv("MOBILE_NO2"),
			os.Getenv("MOBILE_NO3"),
		}

		for _, toNumber := range toNumbers {
			params := twilioApi.CreateMessageParams{
				From: &fromNumber,
				To:   &toNumber,
				Body: &message,
			}

			resp, err := client.Api.CreateMessage(&params)
			if err != nil {
				fmt.Println("error sending sms message: " + err.Error())
			} else {
				response, _ := json.Marshal(*resp)
				fmt.Println("response: " + string(response))
			}
		}
	}
	return nil
}
