# Multi-Threaded TCP Group Chat Server

A concurrent TCP chat server written in **C** with `pthreads` and sockets.  
Originally built as part of a **Systems Programming course project** at Simon Fraser University: CMPT 201.  

---

## 📌 Project Highlights

- **Multi-threaded server** — accepts multiple clients concurrently and relays messages to all participants.  
- **Global ordering** — ensures all clients receive messages in the same total order while preserving per-sender FIFO.  
- **Custom messaging protocol** — type-tagged messages (`0` = chat, `1` = shutdown), with embedded sender IP and port.  
- **Two-Phase Commit Protocol** — provides clean termination once all clients complete sending.  
- **Fuzzing client** — generates random messages to stress-test the server while concurrently receiving and logging outputs.  
- **Scalable testing** — validated with 100+ clients and hundreds of messages per client.  

---
## ⚙️ Requirements

- **Operating System:** Linux (tested on Ubuntu/Docker and WSL).  
  *Note: This project uses POSIX threads and sockets, so it is not portable to Windows without major changes.*  
- **Compiler:** clang (tested with clang 14+).  
- **Build tools:** cmake, make.  
---

## 🖥️ Usage

### Build
```bash
mkdir build
cd build
cmake ..
make
```
* This produces two executables:

  * ./server — the multi-threaded TCP chat server

  * ./client — the fuzzing client

### Run the Server
  ```bash
  ./server <port> <#clients>
  ```

  * Example 
  ```bash
  ./server 8000 3
  ```

### Run a Client
  ```bash
  ./client <server_ip> <port> <#messages> <log_file>
  ```
  * Example 
  ```bash
  ./client 127.0.0.1 8000 10 client0.log
  ```
---
## ✅ Testing

This project was validated using the official course-provided test harness (`server-tester` and `client-tester`).  

The tests covered:
- Basic connectivity and message echoing.  
- Message formatting and protocol compliance.  
- Ordering guarantees under multiple clients.  
- Graceful termination via type `1` messages.  
- Stress tests with 100+ concurrent clients and 100+ messages each.  

In addition, manual testing can be performed with:
```bash
telnet 127.0.0.1 8000

