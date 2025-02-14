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
	"github.com/sirupsen/logrus"
	"github.com/supabase-community/supabase-go"
	"github.com/twilio/twilio-go"
	twilioApi "github.com/twilio/twilio-go/rest/api/v2010"
	"net/http"
	"os"
	"time"
)

var log = logrus.New()

// New status request structure
type waterStatus struct {
	Status string `json:"status"` // "start" or "end"
	Date   string `json:"date"`   // date in "d-M-Y" format
}

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

const (
	ACCOUNT_SID_ENV        = "TWILIO_ACCOUNT_SID"
	ACCOUNT_AUTH_TOKEN_ENV = "TWILIO_AUTH_TOKEN"
	TWILIO_NUMBER          = "TWILIO_NUMBER"
	LOCATION               = "Asia/Kolkata"
)

var debug string
var enableSMS string

func init() {
	// Configure logging
	log.SetFormatter(&logrus.JSONFormatter{})

	if os.Getenv("LOG_LEVEL") != "" {
		level, err := logrus.ParseLevel(os.Getenv("LOG_LEVEL"))
		if err != nil {
			log.SetLevel(logrus.InfoLevel)
		} else {
			log.SetLevel(level)
		}
	} else {
		log.SetLevel(logrus.InfoLevel)
	}

	err := godotenv.Load()
	if err != nil {
		log.Warning("error loading .env file")
	}
	debug = os.Getenv("DEBUG")
	enableSMS = os.Getenv("ENABLE_SMS")

	log.Info("application initialized", logrus.Fields{
		"debug":       debug,
		"sms_enabled": enableSMS,
		"location":    LOCATION,
	})
}

func main() {
	r := gin.Default()
	r.POST("/default/tapwater", TapWaterStatusHandler)
	r.GET("/default/tapwater/start", TapWaterStartGetHandler)

	if debug == "true" {
		log.Info("starting server in debug mode")
		if err := r.Run("localhost:8080"); err != nil {
			log.Fatal("failed to start server", logrus.Fields{
				"error": err,
			})
		}
	} else {
		log.Info("starting server in lambda mode")
		ginLambda = ginadapter.New(r)
		lambda.Start(Handler)
	}
}

func Handler(ctx context.Context, req events.APIGatewayProxyRequest) (events.APIGatewayProxyResponse, error) {
	log.Info("processing lambda request", logrus.Fields{
		"request_id": req.RequestContext.RequestID,
		"path":       req.Path,
		"method":     req.HTTPMethod,
	})
	return ginLambda.ProxyWithContext(ctx, req)
}

func createSupabaseClient() (*supabase.Client, error) {
	supabaseUrl := os.Getenv("SUPABASE_URL")
	supabaseKey := os.Getenv("SUPABASE_KEY")

	log.Debug("creating supabase client", logrus.Fields{
		"url": supabaseUrl,
	})

	return supabase.NewClient(supabaseUrl, supabaseKey, nil)
}

func TapWaterStatusHandler(ctx *gin.Context) {
	requestLogger := log.WithFields(logrus.Fields{
		"handler": "TapWaterStatusHandler",
	})

	var statusUpdate waterStatus
	if err := ctx.BindJSON(&statusUpdate); err != nil {
		requestLogger.Error("failed to bind JSON", logrus.Fields{
			"error": err,
		})
		ctx.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	location, _ := time.LoadLocation(LOCATION)
	currentTime := time.Now().In(location)
	currentDate := currentTime.Format("2-1-2006") // Using current date instead of request body

	requestLogger = requestLogger.WithFields(logrus.Fields{
		"status": statusUpdate.Status,
		"date":   currentDate,
	})
	requestLogger.Info("received status update")

	supabaseClient, err := createSupabaseClient()
	if err != nil {
		requestLogger.Error("database connection failed", logrus.Fields{
			"error": err,
		})
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Database connection failed"})
		return
	}

	switch statusUpdate.Status {
	case "start":
		requestLogger.Info("processing water start status")
		startRecord := tapWaterStartTime{
			Date:      currentDate,
			StartTime: currentTime.Format("15:04"),
		}

		_, _, err = supabaseClient.From("start_time").
			Insert(startRecord, false, "", "", "").
			Execute()
		if err != nil {
			requestLogger.Error("failed to save start time", logrus.Fields{
				"error":  err,
				"record": startRecord,
			})
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to save start time"})
			return
		}
		requestLogger.Info("saved start time record")

		initialRecord := tapWater{
			Date:      fmt.Sprintf("%s, %s", currentTime.Format("Mon"), currentDate),
			StartTime: currentTime.Format("3:04 PM"),
		}

		_, _, err = supabaseClient.From("tapwaterdb").
			Insert(initialRecord, false, "", "", "").
			Execute()
		if err != nil {
			requestLogger.Error("failed to create initial record", logrus.Fields{
				"error":  err,
				"record": initialRecord,
			})
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to create initial record"})
			return
		}
		requestLogger.Info("created initial tapwater record")

		sendArrivalSMS()

	case "end":
		requestLogger.Info("processing water end status")
		var startTimeRecord tapWaterStartTime
		_, err = supabaseClient.From("start_time").
			Select("*", "", false).
			Eq("date", currentDate).
			Single().
			ExecuteTo(&startTimeRecord)

		if err != nil {
			requestLogger.Error("failed to retrieve start time", logrus.Fields{
				"error": err,
				"date":  currentDate,
			})
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to retrieve start time"})
			return
		}
		requestLogger.Info("retrieved start time record")

		startTimeStr := startTimeRecord.StartTime
		startTimeParsed, _ := time.Parse("15:04", startTimeStr)
		startTimeToday := time.Date(
			currentTime.Year(), currentTime.Month(), currentTime.Day(),
			startTimeParsed.Hour(), startTimeParsed.Minute(), 0, 0, location)

		duration := int(currentTime.Sub(startTimeToday).Minutes())

		record := tapWater{
			Date:      fmt.Sprintf("%s, %s", currentTime.Format("Mon"), currentDate),
			StartTime: startTimeToday.Format("3:04 PM"),
			EndTime:   currentTime.Format("3:04 PM"),
			Duration:  duration,
		}

		filter := map[string]string{
			"date":       record.Date,
			"start_time": record.StartTime,
		}

		_, _, err = supabaseClient.From("tapwaterdb").
			Upsert(record, "", "", "").
			Match(filter).
			Execute()

		if err != nil {
			requestLogger.Error("failed to update record", logrus.Fields{
				"error":  err,
				"record": record,
				"filter": filter,
			})
			ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Failed to update record"})
			return
		}
		requestLogger.Info("updated tapwater record with end time")

		sendDepartureSMS(record)

		_, _, err = supabaseClient.From("start_time").
			Delete("", "").
			Eq("date", currentDate).
			Execute()

		if err != nil {
			requestLogger.Warn("failed to delete start time record", logrus.Fields{
				"error": err,
				"date":  currentDate,
			})
		} else {
			requestLogger.Info("deleted start time record")
		}

	default:
		requestLogger.Warn("invalid status received")
		ctx.JSON(http.StatusBadRequest, gin.H{"error": "Invalid status"})
		return
	}

	requestLogger.Info("status processed successfully")
	ctx.JSON(http.StatusOK, gin.H{"message": "Status processed successfully"})
}

func TapWaterStartGetHandler(ctx *gin.Context) {
	requestLogger := log.WithFields(logrus.Fields{
		"handler": "TapWaterStartGetHandler",
	})

	const tableName = "start_time"
	var record tapWaterStartTime

	// Use current date instead of query parameter
	location, _ := time.LoadLocation(LOCATION)
	currentTime := time.Now().In(location)
	currentDate := currentTime.Format("2-1-2006")

	requestLogger = requestLogger.WithField("date", currentDate)
	requestLogger.Info("retrieving start time record")

	supabaseClient, err := createSupabaseClient()
	if err != nil {
		requestLogger.Error("database connection failed", logrus.Fields{
			"error": err,
		})
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": "Database connection failed"})
		return
	}

	_, err = supabaseClient.From(tableName).
		Select("*", "", false).
		Eq("date", currentDate).
		Single().
		ExecuteTo(&record)

	if err != nil {
		requestLogger.Error("failed to retrieve record", logrus.Fields{
			"error": err,
			"table": tableName,
		})
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	requestLogger.Info("successfully retrieved start time record")
	ctx.JSON(http.StatusOK, record)
}

func sendArrivalSMS() {
	smsLogger := log.WithFields(logrus.Fields{
		"function": "sendArrivalSMS",
	})

	location, _ := time.LoadLocation(LOCATION)
	timeInIST := time.Now().In(location)
	message := "\nWater has arrived on " + timeInIST.Format("03:04:05 PM")

	smsLogger.Info("sending water arrival SMS", logrus.Fields{
		"time": timeInIST,
	})

	err := sendSMS(message)
	if err != nil {
		smsLogger.Error("failed to send arrival SMS", logrus.Fields{
			"error": err,
		})
	}
}

func sendDepartureSMS(record tapWater) {
	smsLogger := log.WithFields(logrus.Fields{
		"function": "sendDepartureSMS",
		"record":   record,
	})

	duration := fmt.Sprint(record.Duration)
	message := "\nDate: " + record.Date +
		"\nStart Time: " + record.StartTime +
		"\nEnd Time: " + record.EndTime +
		"\nDuration: " + duration + " minutes"

	if record.Duration != 0 {
		smsLogger.Info("sending water departure SMS")
		err := sendSMS(message)
		if err != nil {
			smsLogger.Error("failed to send departure SMS", logrus.Fields{
				"error": err,
			})
		}
	} else {
		smsLogger.Info("skipping departure SMS due to zero duration")
	}
}

func sendSMS(message string) error {
	smsLogger := log.WithFields(logrus.Fields{
		"function": "sendSMS",
	})

	if os.Getenv("ENABLE_SMS") == "true" {
		accountSid := os.Getenv(ACCOUNT_SID_ENV)
		authToken := os.Getenv(ACCOUNT_AUTH_TOKEN_ENV)
		if accountSid == "" || authToken == "" {
			smsLogger.Fatal("twilio credentials not found")
		}

		client := twilio.NewRestClientWithParams(twilio.ClientParams{
			Username: accountSid,
			Password: authToken,
		})

		fromNumber := os.Getenv(TWILIO_NUMBER)
		toNumbers := []string{
			os.Getenv("MOBILE_NO1"),
			os.Getenv("MOBILE_NO2"),
		}

		smsLogger = smsLogger.WithFields(logrus.Fields{
			"from": fromNumber,
			"to":   toNumbers,
		})
		smsLogger.Info("sending SMS messages")

		for _, toNumber := range toNumbers {
			params := twilioApi.CreateMessageParams{
				From: &fromNumber,
				To:   &toNumber,
				Body: &message,
			}

			resp, err := client.Api.CreateMessage(&params)
			if err != nil {
				smsLogger.Error("failed to send SMS", logrus.Fields{
					"error": err,
					"to":    toNumber,
				})
			} else {
				response, _ := json.Marshal(*resp)
				smsLogger.Info("SMS sent successfully", logrus.Fields{
					"to":       toNumber,
					"response": string(response),
				})
			}
		}
	} else {
		smsLogger.Info("SMS sending is disabled")
	}
	return nil
}
