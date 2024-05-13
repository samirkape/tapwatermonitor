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
	"time"

	"log"
	"net/http"
	"os"
	"strings"
)

type tapWaterStartTime struct {
	Date      string `json:"date,omitempty"`
	StartTime int64  `json:"start_time,omitempty"`
}

type tapWater struct {
	Date      string `json:"date,omitempty"`
	StartTime string `json:"start_time,omitempty"`
	EndTime   string `json:"end_time,omitempty"`
	Duration  int    `json:"duration,omitempty"`
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
		log.Printf("Error loading .env file")
	}
	debug = os.Getenv("DEBUG")
	enableSMS = os.Getenv("ENABLE_SMS")
}

func main() {
	r := gin.Default()
	r.POST("/default/tapwater", TapWaterHandler)
	r.POST("/default/tapwater/start", TapWaterStartHandler)
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

func TapWaterHandler(ctx *gin.Context) {
	const tableName = "tapwaterdb"
	var record tapWater

	if err := ctx.BindJSON(&record); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	supabaseClient, err := createSupabaseClient()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"cannot connect to Supabase": err.Error()})
		return
	}

	log.Printf("Record: %+v", record)

	sendDepartureSMS(record)

	// upsert record to the database
	_, _, err = supabaseClient.From(tableName).Upsert(record, "", "", "").Eq("start_time", record.StartTime).Execute()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	if record.Date == "" {
		date := strings.Split(record.Date, " ")[1]
		_, _, err = supabaseClient.From("start_time").Delete("", "").Eq("date", date).Execute()
		if err != nil {
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
			return
		}
	}

	log.Printf("record inserted successfully")
	ctx.JSON(http.StatusCreated, "")
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

	res, err := supabaseClient.From(tableName).Select("*", "", false).Eq("date", date).Single().ExecuteTo(&record)
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	log.Printf("Count: %+v", res)
	ctx.JSON(http.StatusOK, record)
}

func TapWaterStartHandler(ctx *gin.Context) {
	const tableName = "start_time"

	var record tapWaterStartTime
	if err := ctx.BindJSON(&record); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	sendArrivalSMS()

	supabaseClient, err := createSupabaseClient()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"cannot connect to Supabase": err.Error()})
		return
	}

	_, _, err = supabaseClient.From(tableName).Insert(record, false, "", "", "").Execute()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	log.Printf("record inserted successfully")
	ctx.JSON(http.StatusOK, gin.H{"message": "record inserted successfully"})
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
	message := "\nDate: " + record.Date + "\nStart Time: " + record.StartTime + "\nEnd Time: " + record.EndTime + "\nDuration: " + duration + " minutes"
	err := sendSMS(message)
	if err != nil {
		log.Printf("error sending departure sms: %v", err)
	}
}

// send sms using twilio
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

		// Define the from and to numbers
		fromNumber := "+12512378296" // replace with your Twilio number
		toNumbers := []string{
			os.Getenv("MOBILE_NO1"),
			os.Getenv("MOBILE_NO2"),
			os.Getenv("MOBILE_NO3"),
		}

		for _, toNumber := range toNumbers {
			// Create the message
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
