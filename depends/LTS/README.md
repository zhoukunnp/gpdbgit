# LTS: Logical Timestamp Service
---

**LTS** (**L**ogical **T**imestamp **S**ervice) 是用 Go 语言开发的一个高性能服务组件，用于提供全局的单调递增的事务时间戳服务，LTS 单点时钟源可提供峰值千万级别的QPS。本文档主要简单介绍 LTS 项目的工程代码结构，编译方法，配置、部署运行注意事项等内容，有关 LTS 更多技术细节和优化思路的图文分享，感兴趣的同学烦请移步到我的 KM 文章（[《LTS: 千万级全局事务时间戳服务组件 —— 背景、架构、特性及使用方法简介》](http://km.oa.com/group/29336/articles/show/387440) 以及 [《LTS: 千万级全局事务时间戳服务组件 —— 性能优化方案的设计与实现》](http://km.oa.com/group/29336/articles/show/383506)），欢迎多提宝贵意见，更欢迎多多打赏 ~

## 1. 编译和生成可执行文件
Makefile 在 LTS/src 根目录下，可以根据操作系统不同生成执行文件，生成的执行文件会放置在 LTS/src/bin 目录下，默认已经生成了 linux 下的可执行文件，生成的两个文件分别是 LTS server 和 client simulator 的可执行二进制文件。同时，项目还提供了三种不同类型的 client 的代码编写示例程序，位于 LTS/src/examples 目录下，make 后每个 client 的可执行文件也会生成在对应的子目录底下。因为目前还没有用 go mod 来管理依赖包（后期琢磨会了更新过来），所以编译之前还需记得将项目的路径加入到您的 GOPATH 当中。编译运行指令如下：
	
```sh
# For linux
make

# For mac os
make os=darwin

# For windows
make os=windows
```


## 2. LTS Server / Client 参数配置
LTS Server 的配置分为两部分：pb 协议文件 和 config.toml 参数配置文件，两个文件分别放置在 LTS/proto 和 LTS/src/conf 目录下，前者是定义 server 和 client 端通讯所涉及到的数据结构，后者是配置 server 端的相关参数。而涉及到 LTS client simulator 的参数则直接写在了 client 的启动脚本的参数里面。

### 2.1 ltsrpc.proto 文件
LTS 利用 ProtoBuffer 协议作为模块之间的数据通信格式，相关的 rpc 方法和 message 数据结构都定义在 ltsrpc.proto 文件当中，而其中与时间戳服务最紧密相关的就是 GetTxnTimestampCtx 这个数据结构。该消息体同时代表着 client 的请求以及 server 的应答，txn\_ts 就代表了返回的时间戳，而其他字段皆可以根据业务增添或者删减。

```go
// For txn timestamp request/response
message GetTxnTimestampCtx {
    uint64 txn_id = 1;
    uint64 txn_ts = 2;
}
```


### 2.2 config.toml 文件
LTS Server 的参数配置项都放在了 LTS/src/conf/config.toml 当中，大部分的参数均可以使用当前的推荐默认值，一些需要根据实际环境变动的配置参数说明如下：

```sh
# 单台 LTS server 的名称，在多节点部署的 LTS 集群当中，不同的 server 名字必须不一样
name = "lts-server"

# LTS server 专用于提供时间戳服务的裸 socket 的 IP 和端口号
txnts-service-url = "127.0.0.1:62389"

# LTS server 用于提供 gRPC 服务的 IP 和端口号，port 建议使用 62379
client-urls = "http://127.0.0.1:62379"

# LTS Cluster 节点之间相互通信的 IP 和 端口号，port 建议使用 62380
peer-urls = "http://127.0.0.1:62380"

# 多节点集群部署方式的节点参数，格式为如下，以逗号分割：
# "{nam-1}={peer-urls-1},{name-2}={peer-urls-2},{name-3}={peer-urls-3}"
# "s1=http://127.0.0.1:62380,s2=http://127.0.1.1:62380,s3=http://127.0.2.1:62380"
initial-cluster = "lts-server=http://127.0.0.1:62380"
```


## 3. LTS 的部署与运行
启动/停止 LTS server/client 的脚本都放置在 LTS/src/sbin 目录下，clean\_up.sh 用于删除 LTS Server 所有数据，而示例 client 程序均可以直接在 server 端启动后，直接执行对应的二进制文件即可，如在 LTS/src/examples/http\_client 目录下，直接运行 ./http\_client 即可看到 client 向 LTS server 端发起访问并打印所获取到的时间戳的结果。

### 3.1 运行 LTS Sever 与 client simulator
直接运行 ./start-lts-cluster.sh 即可启动 server 端，文件内容一般情况下不需要修改，但如果修改了 config.toml 当中日志文件名字和目录，则需要对应地修改参数，执行 ./stop-lts-cluster.sh 脚本可停止 server 端。

直接运行 ./start-simulator.sh 即可启动一个 client 模拟器，对应地，执行 ./stop-simulator.sh 脚本便可结束 client 进程。启动脚本当中大部分参数都可以不用更改，必须要配置的参数主要与 LTS server 端以及自身的 ip port 相关：

```sh
# LTS cluster 的 server 地址列表，如部署了多节点集群，则地址之间用逗号分割
# 地址列表与 server 的 config.toml 中的 initial-cluster 对应
lts_cluster_service_url="127.0.0.1:62379,127.0.1.1:62379"

# 对应的事务时间戳的网络地址，与 config.toml 中的 txnts_service_url 对应
txnts_service_url="127.0.0.1:62389"

# Client 模拟器自身的网络地址
client_simulator_url="127.0.0.1:8090"
```

client simulator 中一些可以根据实际情况调整的参数有：

```sh
# worker 是指一个 simulator 内所启动的工作协程数量，默认为 16
--worker-amount 16

# Request Header 的长度，对应 config.toml 中 tcp-max-header-length，默认值为 2 字节
--req-header-length 2

```

以下为运行 http\_client 示例的输出结果，client 每秒向 LTS server 获取一个时间戳：

```go
Receive GetTxnTimestamp resp: 6720424096435273729
Receive GetTxnTimestamp resp: 6720424105025208321
Receive GetTxnTimestamp resp: 6720424109320175617
Receive GetTxnTimestamp resp: 6720424109320175618
Receive GetTxnTimestamp resp: 6720424117910110209
Receive GetTxnTimestamp resp: 6720424122205077505
Receive GetTxnTimestamp resp: 6720424122205077506
Receive GetTxnTimestamp resp: 6720424130795012097
Receive GetTxnTimestamp resp: 6720424135089979393
Receive GetTxnTimestamp resp: 6720424139384946689
http client example ends. Exit.

```


### 3.2 HTTP Curl API

目前因为 LTS 只提供获取事务时间戳的功能，因此 LTS 当前版本的 api 主要跟获取时间戳以及获取集群信息相关。

- **获取集群信息**

	`curl -XGET http://127.0.0.1:62379/lts-cluster/api/cluster-config`
	
- **获取当前集群 Leader**	

	`curl -XGET http://127.0.0.1:62379/lts-cluster/api/leader`
	
- **令当前 Leader Resign，从而切主**

	`curl -XGET http://127.0.0.1:62379/lts-cluster/api/leader/resign`
	
- **设置日志当前的等级: panic / fatal / error / warn / info / debug**

	`curl -XPUT http://127.0.0.1:62379/lts-cluster/api/logger/{level}`

- **获取指定个数的事务时间戳**	
	
	`curl -XGET http://127.0.0.1:62379/lts-cluster/api/ts/{count}`