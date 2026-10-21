# Banking Management System — CS-513 System Software Mini Project

A terminal-based client/server banking simulation written in C for Linux. It uses TCP sockets, POSIX file I/O, binary record files, `fcntl()` record locks, `fork()`, `pthread`, a named POSIX semaphore, and server-side role checks. No SQLite or external database server is used.

## Features mapped to the assignment

### Customer
- Login with one active session per user
- View balance, deposit, withdraw, and transfer funds
- View recent transaction/passbook records
- Apply for a loan and submit feedback
- Change password, logout, and exit

### Bank employee
- Add customer accounts and opening balances
- Modify customer display names
- View loans assigned to that employee
- Approve or reject assigned loan applications with remarks
- View a customer's transaction/passbook records
- Change password and logout

### Manager
- Activate/deactivate customer accounts
- View pending loan applications
- Assign a pending loan to an active employee
- Review recent customer feedback
- List active employees
- Change password and logout

### Administrator
- Add employees
- Modify usernames and customer display names
- Manage user roles
- List users
- Change password and logout

## Architecture

```text
Terminal client
    | TCP stream socket (common/network.c)
    v
Server listener (server/main.c)
    | fork() per client; worker pthread in child; named semaphore caps workers
    v
Connection/request dispatcher (server/connection_handler.c)
    |-- auth.c: authentication, password change, single-session lock
    |-- customer_ops.c: balance, deposit, withdrawal, transfer, passbook
    |-- db.c: binary-file record storage using POSIX I/O
    |-- lock.c: fcntl record locks and ordered two-account locking
    v
 data/users.dat, accounts.dat, transactions.dat, loans.dat, feedback.dat
```

## Build

Required tools on Ubuntu:

```bash
sudo apt update
sudo apt install build-essential
```

From this directory:

```bash
make
```

This builds the server, client, seed utility, and test programs under `bin/`.

## Start the application

Terminal 1:

```bash
./bin/server             # default TCP port 8080
# or: ./bin/server 8081
```

Terminal 2:

```bash
./bin/client             # default 127.0.0.1:8080
# or: ./bin/client 127.0.0.1 8081
```

Keep the server running while clients use the system. The server terminal logs client connections and banking requests; banking menus appear in client terminals.

## Seed data and default credentials

The ZIP includes fresh seeded data files. If you need to reset the database, stop the server first, then run:

```bash
make clean-data
make seed
```

**Warning:** `make clean-data` deletes all users, accounts, transactions, loans, and feedback records before reseeding.

| Role | Username | Password | User ID | Account ID / opening balance |
|---|---|---|---:|---|
| Administrator | `admin` | `adminpassword` | 1001 | — |
| Manager | `manager1` | `managerpassword` | 1002 | — |
| Employee | `employee1` | `employeepassword` | 1003 | — |
| Customer | `alice` | `alicepassword` | 1004 | 2001 / $15,000 |
| Customer | `bob` | `bobpassword` | 1005 | 2002 / $8,000 |

A sample pending loan and sample feedback are included by the seeder. User IDs and account IDs are assigned from the current record files.

## Test suites

Run tests with the server stopped and from this directory:

```bash
make test-locks
make test-auth
make test-customer
make test-transfers
```

Tests exercise session-lock exclusion, login/password behavior, balance and transfer rules, overdraft rejection, and concurrent transfer locking. Some tests modify the seeded database; run `make clean-data && make seed` after testing if you want the original sample data back.

## System Software mechanisms used

- `socket()`, `bind()`, `listen()`, `accept()`, `connect()` — TCP client/server
- `fork()`, `waitpid()`, `sigaction()` — client process isolation and child reaping
- `pthread_create()`, `pthread_join()` — worker thread in each client process
- `sem_open()`, `sem_wait()`, `sem_post()` — bound simultaneous client workers
- `open()`, `read()`, `write()`, `lseek()`, `fsync()`, `close()` — binary record files
- `fcntl()` read/write record locks — session and account coordination
- `send()`/`recv()` wrappers in `common/network.c` — exact packet transfer over TCP

## Design notes and limitations

This is an educational mini-project, not production banking software. It uses plaintext demo passwords and native C structs as on-disk/network records, so files and packets are intended for the same build/platform. Record locks and ordered account locking protect key account operations, but multi-file updates (for example, account balance plus transaction ledger) do not have a write-ahead log or complete crash-recovery rollback. Therefore, do not claim full crash-safe ACID guarantees. The implementation demonstrates consistency checks, durable account writes, and concurrency control, while full atomicity/recovery would require a journal or transaction log.
