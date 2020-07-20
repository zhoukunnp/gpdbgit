package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"github.com/gogo/protobuf/proto"
	"io"
	"git.code.oa.com/tdsql_util/LTS/ltsproto/ltsrpc"
	"net"
	"sync"
	"time"
	"reflect"
	"math"
)

// This package shows the usage of a tcp LTS client.

var (
	headerLen uint32 = 2  // Same as ltsconf.Cfg().TCP.TCPMaxHeaderLength
	bodyLen   uint32 = 16 // Will be updated by init()
)

// init calculates the maximum length of marshaled GetTxnTimestampCtx
func init() {
	mockReq := reflect.New(reflect.TypeOf(ltsrpc.GetTxnTimestampCtx{}))
	for i := 0; i < mockReq.Elem().NumField(); i++ {
		switch mockReq.Elem().Field(i).Kind() {
		case reflect.Uint64:
			mockReq.Elem().Field(i).SetUint(math.MaxUint64)
		case reflect.Uint32:
			mockReq.Elem().Field(i).SetUint(math.MaxUint32)
		case reflect.Uint16:
			mockReq.Elem().Field(i).SetUint(math.MaxUint16)
		case reflect.Uint8:
			mockReq.Elem().Field(i).SetUint(math.MaxUint8)
		case reflect.Int64:
			mockReq.Elem().Field(i).SetUint(math.MaxInt64)
		case reflect.Int32:
			mockReq.Elem().Field(i).SetUint(math.MaxInt32)
		case reflect.Int16:
			mockReq.Elem().Field(i).SetUint(math.MaxInt16)
		case reflect.Int8:
			mockReq.Elem().Field(i).SetUint(math.MaxInt8)
		case reflect.Bool:
			mockReq.Elem().Field(i).SetBool(true)
		case reflect.Int:
			mockReq.Elem().Field(i).SetInt(math.MaxInt64)
		case reflect.Uint:
			mockReq.Elem().Field(i).SetUint(math.MaxUint64)
		}
	}

	tmpReq := mockReq.Elem().Interface().(ltsrpc.GetTxnTimestampCtx)
	tmpBuf, _ := proto.Marshal(&tmpReq)

	bodyLen = uint32(len(tmpBuf))
	fmt.Printf("[INIT] Maximum length of marshaled GetTxnTimestampCtx is: %v\n", bodyLen)
}


func main() {

	// Establish a TCP connection with the raw socket channel of LTS server
	rawTSServiceUrl := "127.0.0.1:62389"
	tcpAddr, _ := net.ResolveTCPAddr("tcp", rawTSServiceUrl)
	ltsConn, dialErr := net.DialTCP("tcp", nil, tcpAddr)

	if dialErr != nil {
		fmt.Printf("Dial LTS server error: %v\n", dialErr)
		return
	}

	// Construct a GetTxnTimestampCtx entity and marshal it to []byte
	req := &ltsrpc.GetTxnTimestampCtx{
		TxnId: 12345678,
	}

	maxBufLength := headerLen + bodyLen
	reqBuffer := make([]byte, maxBufLength, maxBufLength)

	dataBuf, _ := proto.Marshal(req)

	// Fill in the header buffer and data buffer respectively
	dataLen := copy(reqBuffer[headerLen:], dataBuf)
	binary.BigEndian.PutUint16(reqBuffer[:bodyLen], uint16(dataLen))

	var wg sync.WaitGroup

	// Receiver routine
	wg.Add(1)
	go func() {
		defer wg.Done()

		// Buffered socket reader
		connReader := bufio.NewReader(ltsConn)

		// Recv buffer and response entity
		recvBuf := make([]byte, maxBufLength, maxBufLength)
		resp := &ltsrpc.GetTxnTimestampCtx{}

		for {
			// Each time, read exactly len(recvBuf) bytes from the socket
			_, recvErr := io.ReadFull(connReader, recvBuf)
			if recvErr != nil {
				fmt.Printf("Recv data error: %v\n", recvErr)
				return
			}

			// Parsed the uint64 result from the bytes buffer
			respLen := uint32(binary.BigEndian.Uint16(recvBuf[:headerLen]))
			if unmarshalErr := proto.Unmarshal(recvBuf[headerLen:headerLen+respLen], resp); unmarshalErr != nil {
				fmt.Printf("Unmarshal response entity error: %v\n", unmarshalErr)
				continue
			}

			fmt.Printf("Receive timestamp: %v\n", resp.TxnTs)
		}
	}()

	// Sender sends a couple of requests
	for i := 0; i < 10; i++ {
		if _, sendErr := ltsConn.Write(reqBuffer); sendErr != nil {
			fmt.Printf("Send request err: %v\n", sendErr)
			return
		}

		time.Sleep(time.Second)
	}

	if closeErr := ltsConn.Close(); closeErr != nil {
		fmt.Printf("Failed to close connection: %v\n", closeErr)
	}

	wg.Wait()

	fmt.Print("tcp client example ends. Exit.\n")
}
