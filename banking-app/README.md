# Banking Management System

A multi-client, concurrent banking management system implemented in C using POSIX UNIX System Calls, Socket Programming, and `fcntl` record locking.

## Course Information
- **Course**: CS-513 System Software – Mini Project
- **Institution**: International Institute of Information Technology, Bangalore

---

## Architecture Overview
The system follows a Client-Server architecture over TCP sockets where:
- **Server**: Maintains binary record databases on disk, manages concurrency, handles requests using worker processes/threads, and guarantees ACID properties using `fcntl` record-level locking.
- **Client**: A responsive terminal CLI allowing authenticated users (Customer, Bank Employee, Manager, Administrator) to interact with the banking backend.

## Project Structure
```
banking-app/
├── Makefile                # Build automation
├── common/
│   ├── models.h            # Data structures (User, Account, Transaction, Loan, Feedback)
│   └── protocol.h          # Request opcodes, response status codes, network packets
├── server/
│   └── main.c              # Server entry point
├── client/
│   └── main.c              # Client entry point
├── bin/                    # Compiled binaries (server, client, seed_db)
└── data/                   # Binary database records (*.dat)
```

## Compilation
To compile the system:
```bash
make
```

To clean compiled binaries:
```bash
make clean
```
