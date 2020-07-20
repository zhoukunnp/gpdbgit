package main

import (
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"google.golang.org/grpc"
	"context"
	"fmt"
	"time"
)

// This package shows the usage of a gRPC LTS client.
func main() {

	// Establish network connection with LTS server
	serverAddr := "127.0.0.1:62379"
	conn, err := grpc.Dial(serverAddr, grpc.WithInsecure())

	defer func() {
		if closeErr := conn.Close(); closeErr != nil {
			fmt.Printf("Failed to close gRPC connection: %v\n", closeErr)
		}
	}()

	if err != nil {
		fmt.Printf("Failed to connect to lts server: %v\n", err)
		return
	}

	// Create a gRPC client stub
	ltsClient := ltsrpc.NewLTSClient(conn)

	// Invoke gRPC method to obtain timestamp for a couple of times
	for i := 0; i < 10; i++ {
		resp, err := ltsClient.GetTxnTimestamp(context.Background(), &ltsrpc.GetTxnTimestampCtx{TxnId: 123})
		if err != nil {
			fmt.Printf("Get txn ts rpc error: %v\n", err)
			return
		}

		fmt.Printf("Receive GetTxnTimestamp resp: %v\n", resp.TxnTs)
		time.Sleep(time.Second)
	}

	fmt.Print("gRPC client example ends. Exit.\n")

	return
}
