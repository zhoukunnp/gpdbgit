package main

import (
	"fmt"
	"os/exec"
	"strconv"
	"time"
)

// This package shows the usage of a http LTS client.
func main() {

	reqStr := "curl -XGET http://127.0.0.1:62379/lts-cluster/api/ts/1"
	for i := 0; i < 10; i++ {
		// Send http request using curl api
		resp := exec.Command("sh", "-c", reqStr)
		out, err := resp.Output()
		if err != nil {
			fmt.Printf("Execute curl api error: %v\n", err)
			return
		}

		// Parse the uint64 result from the returned string
		ts, parseErr := strconv.ParseUint(string(out), 10, 64)
		if parseErr != nil {
			fmt.Printf("Invalid return format: %v\n", parseErr)
			return
		}

		fmt.Printf("Receive GetTxnTimestamp resp: %v\n", ts)
		time.Sleep(time.Second)
	}

	fmt.Print("http client example ends. Exit.\n")

	return
}
