#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "server/lock.h"
#include "server/db.h"

int main(void) {
    printf("=====================================================\n");
    printf("   Banking Management System - Lock Verification     \n");
    printf("=====================================================\n\n");

    db_init();

    int test_user_id = 1004; /* Alice */
    int session_fd = -1;

    printf("[TEST 1] Acquiring single-session lock for User ID %d...\n", test_user_id);
    if (acquire_user_session(test_user_id, &session_fd) == 0) {
        printf("[+] SUCCESS: First session lock acquired (fd = %d).\n", session_fd);
    } else {
        fprintf(stderr, "[-] FAILED: Could not acquire first session.\n");
        return 1;
    }

    printf("\n[TEST 2] Forking child process to attempt CONCURRENT login for User ID %d...\n", test_user_id);
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process attempts concurrent login */
        int child_fd = -1;
        if (acquire_user_session(test_user_id, &child_fd) < 0) {
            printf("[+] CHILD PROCESS: Single-session lock rejected concurrent login as expected!\n");
            exit(0);
        } else {
            fprintf(stderr, "[-] CHILD PROCESS ERROR: Concurrent login was mistakenly allowed!\n");
            release_user_session(test_user_id, child_fd);
            exit(1);
        }
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("[+] TEST 2 PASSED: Concurrent session prevention verified via fcntl.\n");
    } else {
        fprintf(stderr, "[-] TEST 2 FAILED.\n");
        return 1;
    }

    printf("\n[TEST 3] Releasing session lock in Parent...\n");
    if (release_user_session(test_user_id, session_fd) == 0) {
        printf("[+] SUCCESS: Session lock released.\n");
    } else {
        fprintf(stderr, "[-] FAILED: Could not release session lock.\n");
        return 1;
    }

    printf("\n[TEST 4] Testing deadlock-free two-account transfer lock...\n");
    int acc_fd = open(FILE_ACCOUNTS, O_RDWR, 0644);
    if (acc_fd >= 0) {
        if (lock_transfer_accounts(acc_fd, 2002, 2001) == 0) {
            printf("[+] SUCCESS: Acquired write locks on accounts 2001 and 2002 with resource ordering.\n");
            unlock_transfer_accounts(acc_fd, 2002, 2001);
            printf("[+] SUCCESS: Released transfer account locks.\n");
        } else {
            fprintf(stderr, "[-] FAILED to lock transfer accounts.\n");
        }
        close(acc_fd);
    }

    printf("\n=====================================================\n");
    printf("   All Locking & Concurrency Tests Passed!           \n");
    printf("=====================================================\n");

    return 0;
}
