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
│   ├── main.c              # Server entry point
│   ├── db.h                # POSIX File I/O persistence declarations
│   └── db.c                # Low-level binary file CRUD operations
├── client/
│   └── main.c              # Client entry point
├── scripts/
│   └── seed_db.c           # Utility to initialize database and seed sample records
├── bin/                    # Compiled binaries (server, client, seed_db)
└── data/                   # Binary database records (*.dat)
```

## Compilation & Seeding
To compile all targets (server, client, database seeder):
```bash
make
```

To initialize and seed the database with initial accounts:
```bash
make seed
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
