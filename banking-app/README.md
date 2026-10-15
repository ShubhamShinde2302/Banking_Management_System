# Banking Management System

A multi-client, concurrent banking management system implemented in C using POSIX UNIX System Calls, Socket Programming, and `fcntl` record locking.

## Course Information
- **Course**: CS-513 System Software – Mini Project
- **Institution**: International Institute of Information Technology, Bangalore

---

## Architecture Overview
The system follows a Client-Server architecture over TCP sockets where:
- **Server**: Maintains binary record databases on disk, manages concurrency using a `fork()`-per-client multi-process model with `SIGCHLD` zombie reaping, and guarantees ACID properties using `fcntl` record-level locking.
- **Locking & Concurrency Subsystem**: Implements granular `fcntl` advisory read/write record locks on accounts, deadlock-free two-phase locking for funds transfer, and strict **single-session enforcement** per user across concurrent terminals.
- **Client**: A responsive terminal CLI allowing authenticated users (Customer, Bank Employee, Manager, Administrator) to interact with the banking backend over stream sockets.

## Project Structure
```
banking-app/
├── Makefile                # Build automation
├── common/
│   ├── models.h            # Data structures (User, Account, Transaction, Loan, Feedback)
│   ├── protocol.h          # Request opcodes, response status codes, network packets
│   ├── network.h           # Socket stream transmission definitions
│   └── network.c           # Low-level exact read/write packet helpers
├── server/
│   ├── main.c              # Server entry point, TCP listener & multi-process dispatcher
│   ├── connection_handler.h# Per-client process request dispatcher declarations
│   ├── connection_handler.c# Per-client connection loop and session cleanup
│   ├── lock.h              # fcntl record locking & single-session manager declarations
│   ├── lock.c              # Granular record-level locking implementation
│   ├── db.h                # POSIX File I/O persistence declarations
│   └── db.c                # Low-level binary file CRUD operations
├── client/
│   └── main.c              # Client entry point and socket communication
├── scripts/
│   ├── seed_db.c           # Utility to initialize database and seed sample records
│   └── test_locks.c        # Concurrency & record locking test suite
├── bin/                    # Compiled binaries (server, client, seed_db, test_locks)
└── data/                   # Binary database records (*.dat)
```

## Compilation & Verification
To compile all targets:
```bash
make
```

To seed initial database records:
```bash
make seed
```

To run the record-level locking and concurrency verification suite:
```bash
make test-locks
```

## Running the Application
1. **Start the Banking Server**:
```bash
./bin/server [port]       # Defaults to port 8080 if not specified
```

2. **Start a Client Terminal** (in another terminal):
```bash
./bin/client [host] [port] # Defaults to 127.0.0.1:8080
```

To clean compiled binaries:
```bash
make clean
```

To clean binary database records:
```bash
make clean-data
```

## Seeded Default Test Credentials
| Role | Username | Password | User ID | Account ID / Initial Balance |
| :--- | :--- | :--- | :--- | :--- |
| **Administrator** | `admin` | `adminpassword` | 1001 | N/A |
| **Manager** | `manager1` | `managerpassword` | 1002 | N/A |
| **Bank Employee** | `employee1` | `employeepassword` | 1003 | N/A |
| **Customer 1** | `alice` | `alicepassword` | 1004 | 2001 (₹15,000.00) |
| **Customer 2** | `bob` | `bobpassword` | 1005 | 2002 (₹8,000.00) |
