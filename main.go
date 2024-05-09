package main

import (
	"context"
	"github.com/aws/aws-lambda-go/events"
	"github.com/aws/aws-lambda-go/lambda"
	ginadapter "github.com/awslabs/aws-lambda-go-api-proxy/gin"
	"github.com/gin-gonic/gin"
	"github.com/google/uuid"
	"github.com/joho/godotenv"
	"github.com/supabase-community/supabase-go"
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

func Handler(ctx context.Context, req events.APIGatewayProxyRequest) (events.APIGatewayProxyResponse, error) {
	log.Printf("Processing Lambda request %s\n", req.RequestContext.RequestID)
	return ginLambda.ProxyWithContext(ctx, req)
}

func init() {
	err := godotenv.Load()
	if err != nil {
		log.Fatal("Error loading .env file")
	}
}

func main() {

	r := gin.Default()
	r.POST("/default/tapwater", TapWaterHandler)
	r.POST("/default/tapwater/start", TapWaterStartHandler)
	r.GET("/default/tapwater/start", TapWaterStartGetHandler)
	//
	//if err := r.Run("localhost:8080"); err != nil {
	//	panic("Failed to start server")
	//}
	ginLambda = ginadapter.New(r)
	lambda.Start(Handler)
}

func TapWaterHandler(ctx *gin.Context) {
	const tableName = "tapwaterdb"
	id := uuid.New()

	supabaseUrl := os.Getenv("SUPABASE_URL")
	supabaseKey := os.Getenv("SUPABASE_KEY")

	var record tapWater
	if err := ctx.BindJSON(&record); err != nil {
		ctx.JSON(http.StatusBadRequest, gin.H{"error": err.Error()})
		return
	}

	log.Printf("Record: %+v", record)

	supabaseClient, err := supabase.NewClient(supabaseUrl, supabaseKey, nil)
	if err != nil {
		log.Fatal("cannot connect to Supabase")
	}

	// upsert record to the database
	_, _, err = supabaseClient.From(tableName).Upsert(record, "", "", "").Execute()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	// trim first 5 characters from the date
	date := strings.Split(record.Date, " ")[1]
	_, _, err = supabaseClient.From("start_time").Delete("", "").Eq("date", date).Execute()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	log.Printf("Record inserted successfully")
	ctx.JSON(http.StatusOK, id)
}

func TapWaterStartGetHandler(ctx *gin.Context) {
	const tableName = "start_time"

	supabaseUrl := os.Getenv("SUPABASE_URL")
	supabaseKey := os.Getenv("SUPABASE_KEY")

	var record tapWaterStartTime
	date := ctx.Query("date")
	supabaseClient, err := supabase.NewClient(supabaseUrl, supabaseKey, nil)
	if err != nil {
		log.Fatal("cannot connect to Supabase")
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

	supabaseUrl := os.Getenv("SUPABASE_URL")
	supabaseKey := os.Getenv("SUPABASE_KEY")

	supabaseClient, err := supabase.NewClient(supabaseUrl, supabaseKey, nil)
	if err != nil {
		log.Fatal("cannot connect to Supabase")
	}

	_, _, err = supabaseClient.From(tableName).Insert(record, false, "", "", "").Execute()
	if err != nil {
		ctx.JSON(http.StatusInternalServerError, gin.H{"error": err.Error()})
		return
	}

	log.Printf("Record inserted successfully")
	ctx.JSON(http.StatusOK, gin.H{"message": "record inserted successfully"})
}
