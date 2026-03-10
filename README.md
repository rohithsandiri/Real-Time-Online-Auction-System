# Real-Time Online Auction System

A concurrent, multi-threaded auction platform built in C using Socket Programming, demonstrating core System Programming concepts such as Record-Level File Locking (`fcntl`), POSIX Threads (`pthread`), Mutex Synchronization, Escrow-based Fund Management, and a custom binary protocol over TCP.

## Tech Stack

| Layer            | Technology                                                                     |
| ---------------- | ------------------------------------------------------------------------------ |
| Language         | C (GCC)                                                                        |
| Networking       | TCP Sockets (`socket`, `bind`, `listen`, `accept`)                             |
| Concurrency      | POSIX Threads (`pthread_create`, `pthread_mutex`)                              |
| File Locking     | `fcntl` Advisory Record-Level Locks (`F_RDLCK`, `F_WRLCK`)                     |
| Storage          | Binary flat-files with offset-based random access (`lseek`, `pread`, `pwrite`) |
| Security         | DJB2 password hashing, masked terminal input (`termios`)                       |
| Containerization | Docker, Docker Compose                                                         |
| CI/CD            | Jenkins Pipeline (Build + Push to DockerHub)                                   |

## Architecture

```
┌──────────┐   TCP/8085   ┌───────────────────────────────────────────┐
│  Client   │◄────────────►│              Server                       │
│ (client.c)│   Request/   │                                           │
│  Menu UI  │   Response   │  ┌─────────────┐  ┌────────────────────┐ │
└──────────┘   structs     │  │ Thread Pool  │  │  Auction Monitor   │ │
                           │  │ (per-client) │  │  (background tick) │ │
┌──────────┐               │  └──────┬───────┘  └────────┬───────────┘ │
│  Client   │◄────────────►│         │                    │             │
└──────────┘               │  ┌──────▼────────────────────▼───────────┐│
                           │  │          Handler Layer                 ││
┌──────────┐               │  │  user_handler │ item_handler │ session ││
│  Client   │◄────────────►│  └──────┬────────────────────┬───────────┘│
└──────────┘               │         │    fcntl locks      │            │
                           │  ┌──────▼────────────────────▼───────────┐│
                           │  │       Binary Data Files               ││
                           │  │    data/users.dat  data/items.dat     ││
                           │  └───────────────────────────────────────┘│
                           └───────────────────────────────────────────┘
```

- **Server**: Multi-threaded TCP server. Each client connection spawns a dedicated `pthread`. A background monitor thread auto-closes expired auctions every second.
- **Client**: Menu-driven CLI that communicates with the server using fixed-size `Request`/`Response` structs over TCP.
- **Storage**: Binary flat-files (`users.dat`, `items.dat`) accessed via direct offset calculation (`(id - 1) * sizeof(struct)`), enabling O(1) record lookups.

## Key Functionalities

### User Management

- **Registration** with initial balance and security question
- **Login/Logout** with duplicate session prevention (max 10 concurrent users)
- **Password Hashing** using DJB2 algorithm (passwords are never stored in plaintext)
- **Reset Password** (authenticated) and **Forgot Password** (via security question)
- **Masked Password Input** using `termios` to disable terminal echo

### Auction Operations

- **List Items for Sale** with a time-based duration (minutes)
- **View All Auctions** with live countdown timers
- **Place Bids** with real-time validation (must exceed current highest bid)
- **Close Auction Manually** (seller only) or automatic expiry via background monitor
- **Withdraw Bid** with escrow refund and 2-minute cooldown penalty

### Financial System (Escrow Model)

- On placing a bid, funds are **deducted (escrowed)** from the bidder's balance immediately
- If a higher bid arrives, the **previous bidder is automatically refunded**
- On auction close, escrowed funds are **transferred to the seller**
- On bid withdrawal, the next highest bidder who can afford the escrow is promoted

### Dynamic Menu System

- Menu options adapt based on user state (e.g., "Close Auction" only appears if the user has active listings, "Withdraw Bid" only if they have active bids)

### Logging

- Thread-safe audit logging (`pthread_mutex`) to `logs/server.log`
- Logs: connections, logins/logouts, bids, item listings, auction closures, fund transfers

## Concurrency and Locking Concepts

### 1. Record-Level Locking (`fcntl`)

Unlike whole-file locking, this system uses **byte-range advisory locks** via `fcntl(fd, F_SETLKW, &lock)` to lock individual records. This means:

- User A can bid on **Item #1** while User B simultaneously bids on **Item #2** (different byte ranges, no contention)
- If both bid on the **same item**, the second thread blocks (`F_SETLKW` = blocking wait) until the first completes

```
// Lock a single record at a calculated offset
lock.l_type   = F_WRLCK;           // Exclusive write lock
lock.l_whence = SEEK_SET;
lock.l_start  = (item_id - 1) * sizeof(Item);  // Byte offset
lock.l_len    = sizeof(Item);       // Lock only this record
fcntl(fd, F_SETLKW, &lock);        // Block until lock acquired
```

**Readers-Writer Logic**: Viewing items uses `F_RDLCK` (shared lock, multiple readers allowed), while bidding/updating uses `F_WRLCK` (exclusive lock, blocks all other access).

### 2. Deadlock Prevention (Ordered Locking)

The `transfer_funds()` function must lock **two user records** simultaneously. Without ordering, this classic scenario causes deadlock:

| Thread 1           | Thread 2           |
| ------------------ | ------------------ |
| Lock User A        | Lock User B        |
| Wait for User B... | Wait for User A... |
| **DEADLOCK**       | **DEADLOCK**       |

**Solution**: Always lock the **smaller user ID first**, breaking the circular wait condition:

```c
int first_id  = (from_user_id < to_user_id) ? from_user_id : to_user_id;
int second_id = (from_user_id < to_user_id) ? to_user_id : from_user_id;
// Lock first_id, then second_id -- guaranteed no circular wait
```

### 3. Mutex Synchronization (`pthread_mutex`)

Used for in-memory shared data that `fcntl` cannot protect:

- **Session Array**: `session_lock` mutex prevents race conditions when two threads simultaneously try to log in or detect duplicate sessions
- **Log File**: `log_lock` mutex prevents interleaved log lines when multiple threads write concurrently

### 4. Race Condition: Auction Expiry During Bid

A bid could be placed at the exact moment an auction expires. The system handles this by **re-checking the timer inside the write lock**:

```c
// Already holding exclusive lock on the item record
if (time(NULL) >= item.end_time) {
    // Reject the bid -- auction expired between the user's request and the lock acquisition
    return -4;
}
```

### 5. Race Condition: Withdraw Cascading to Next Bidder

When a bid is withdrawn, the system must find the next highest bidder and re-escrow their funds. But that bidder may have **spent their money elsewhere** between their original bid and now. The system handles this in a loop:

```
While (candidates remain):
    Find highest remaining bid
    Try to deduct (escrow) their funds
    If success -> they are the new winner, break
    If fail   -> disqualify them, try next highest
```

## Test Scenarios

### Scenario 1: Bidding War (Record Locking)

1. Open **two client terminals**, register and log in as User A and User B
2. User A lists an item with base price $100
3. User A (from terminal 1) views items to get the Item ID
4. User B bids $150 on the item -- accepted
5. User A bids $120 -- **rejected** (lower than current bid of $150)
6. User A bids $200 -- accepted, User B is **automatically refunded** $150

### Scenario 2: Concurrent Bids on Same Item (Lock Contention)

1. User A and User B both attempt to bid on the **same item** at the same instant
2. One thread acquires the `F_WRLCK` on the item record first
3. The second thread **blocks** until the first completes
4. The second bid is then validated against the **updated** current bid
5. Verify: no data corruption, the higher bid wins

### Scenario 3: Concurrent Bids on Different Items (No Contention)

1. User A bids on Item #1, User B bids on Item #2 simultaneously
2. Both succeed without blocking each other (different byte ranges locked)

### Scenario 4: Escrow Fund Transfer

1. User A registers with $500 balance, bids $300 on an item
2. Check balance -- should show **$200** (300 escrowed)
3. Seller closes the auction
4. Check User A's balance -- still $200 (escrow transferred to seller)
5. Check Seller's balance -- increased by $300

### Scenario 5: Withdraw and Cooldown

1. User A bids $200 on an item, User B bids $250 (User A is refunded $200)
2. User B withdraws their bid -- refunded $250, **2-minute cooldown** applied
3. User B immediately tries to bid again -- **rejected** (cooldown active)
4. User A is automatically promoted as the highest bidder (if funds available)

### Scenario 6: Auction Auto-Expiry

1. List an item with a **1-minute** duration
2. Wait for the timer to expire
3. The background monitor thread automatically closes the auction
4. Verify: funds transferred to seller, item status changes to "Ended"

### Scenario 7: Deadlock Prevention (Fund Transfer)

1. Simulate two auctions closing simultaneously where:
   - Auction 1: User A (seller) receives funds from User B (winner)
   - Auction 2: User B (seller) receives funds from User A (winner)
2. Without ordered locking, this would deadlock. Verify both transactions complete.

### Scenario 8: Duplicate Login Prevention

1. User A logs in from Terminal 1
2. Try logging in as User A from Terminal 2 -- **rejected** ("User already logged in")
3. Log out from Terminal 1, then retry Terminal 2 -- succeeds

## Project Structure

```
.
├── src/                        # Source files (.c)
│   ├── server.c                # Main server: TCP listener, client thread handler
│   ├── client.c                # Main client: menu-driven UI
│   ├── user_handler.c          # Registration, authentication, balance, password, cooldown
│   ├── item_handler.c          # Item CRUD, bidding, auction close, expiry monitor
│   ├── file_handler.c          # Generic fcntl record lock/unlock wrappers
│   ├── session.c               # In-memory session tracking with mutex
│   └── logger.c                # Thread-safe file logging with mutex
├── include/                    # Header files (.h)
│   ├── common.h                # Shared structs (User, Item, Request, Response), constants
│   ├── user_handler.h          # User handler function prototypes
│   ├── item_handler.h          # Item handler function prototypes
│   ├── file_handler.h          # File lock/unlock function prototypes
│   ├── session.h               # Session management function prototypes
│   └── logger.h                # Logger function prototypes
├── bin/                        # Compiled binaries (gitignored)
├── data/                       # Runtime binary data files (gitignored)
│   ├── users.dat               # User records
│   └── items.dat               # Item/auction records
├── logs/                       # Server log output (gitignored)
│   └── server.log              # Audit log
├── Makefile                    # Build configuration
├── Dockerfile                  # Container image build
├── docker-compose.yml          # Container orchestration
├── Jenkinsfile                 # CI/CD pipeline
└── README.md
```

## Installation and Usage

### Prerequisites

- POSIX-compliant OS (Linux/macOS)
- **For local setup**: GCC compiler
- **For Docker setup**: Docker

### Local Setup

```bash
# Clone the repository
git clone https://github.com/aayanksinghai/Real-Time-Online-Auction-System.git
cd Real-Time-Online-Auction-System

# Build the project (compiles server + client, creates data/ and logs/ directories)
make

# Start the server (in one terminal)
./bin/server

# Start a client (in another terminal, run multiple for testing concurrency)
./bin/client
```

> **Note**: Always run the binaries from the **project root directory** (`./bin/server`, not `cd bin && ./server`) since data and log paths are relative to the working directory.

```bash
# Clean build artifacts and data
make clean
```

### Docker Setup (Pull from DockerHub)

No source code, no compiler needed. Only Docker is required.

```bash
# Step 1: Pull the image and start the server container
docker run -d -p 8085:8085 --name auction-server aayanksinghai/auction-system:latest

# Step 2: Extract the pre-compiled client binary from the container
docker cp auction-server:/usr/src/app/bin/client ./client

# Step 3: Run the client (open multiple terminals to test concurrency)
./client
```

```bash
# Stop the server
docker stop auction-server

# Stop and remove the container
docker stop auction-server && docker rm auction-server
```

### CI/CD Pipeline (Jenkins)

The `Jenkinsfile` defines a three-stage pipeline:

1. **Checkout** -- Clones the repository from GitHub
2. **Build Image** -- Builds the Docker image (compilation happens inside the Dockerfile)
3. **Push to DockerHub** -- Pushes the image as `aayanksinghai/auction-system:latest`

## Protocol

Client and server communicate using fixed-size C structs sent over TCP:

| Direction        | Struct          | Key Fields                                                  |
| ---------------- | --------------- | ----------------------------------------------------------- |
| Client -> Server | `Request`       | `operation`, `username`, `password`, `payload`              |
| Server -> Client | `Response`      | `operation` (SUCCESS/ERROR), `message`, `session_id`        |
| Server -> Client | `DisplayItem`   | Used for item listing (id, name, bid, timer, winner)        |
| Server -> Client | `HistoryRecord` | Used for transaction history (seller/winner names, amounts) |

A custom `recv_all()` function ensures complete struct delivery over TCP, handling partial reads from the kernel buffer.
