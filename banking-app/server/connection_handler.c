#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "server/connection_handler.h"
#include "server/auth.h"
#include "server/customer_ops.h"
#include "server/lock.h"
#include "server/db.h"
#include "common/protocol.h"
#include "common/network.h"

void handle_client(int client_sock, struct sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    pid_t pid = getpid();

    printf("[WORKER %d] Handling connection from %s:%d\n", pid, client_ip, client_port);

    int active_user_id = 0;
    int active_role = 0;
    int active_account_id = 0;
    int session_fd = -1;

    RequestPacket req;
    ResponsePacket res;

    while (1) {
        memset(&req, 0, sizeof(req));
        int ret = recv_request(client_sock, &req);
        if (ret < 0) {
            printf("[WORKER %d] Client %s:%d disconnected abruptly.\n", pid, client_ip, client_port);
            break;
        }

        memset(&res, 0, sizeof(res));

        switch (req.opcode) {
            case REQ_LOGIN: {
                if (active_user_id > 0) {
                    res.status_code = RESP_ERR_BAD_REQUEST;
                    snprintf(res.message, sizeof(res.message),
                             "Already logged in as User ID %d. Please log out first.", active_user_id);
                } else {
                    LoginPayload *login = (LoginPayload *)req.payload;
                    AuthSuccessPayload auth_info;
                    int status = auth_login(login, &auth_info, &session_fd, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        active_user_id = auth_info.user_id;
                        active_role = auth_info.role;
                        active_account_id = auth_info.account_id;
                        memcpy(res.payload, &auth_info, sizeof(AuthSuccessPayload));
                        res.data_len = sizeof(AuthSuccessPayload);
                        printf("[WORKER %d] Authenticated %s (ID %d, %s, Acc %d)\n",
                               pid, auth_info.username, auth_info.user_id,
                               role_to_string(auth_info.role), auth_info.account_id);
                    }
                }
                break;
            }

            case REQ_LOGOUT: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "No active session to log out.");
                } else {
                    auth_logout(active_user_id, session_fd);
                    printf("[WORKER %d] User ID %d logged out.\n", pid, active_user_id);
                    active_user_id = 0;
                    active_role = 0;
                    active_account_id = 0;
                    session_fd = -1;
                    res.status_code = RESP_OK;
                    snprintf(res.message, sizeof(res.message), "Logged out successfully.");
                }
                break;
            }

            case REQ_CHANGE_PASSWORD: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to change password.");
                } else {
                    ChangePasswordPayload *cp = (ChangePasswordPayload *)req.payload;
                    int status = auth_change_password(active_user_id, cp, res.message, sizeof(res.message));
                    res.status_code = status;
                }
                break;
            }

            /* ==================== CUSTOMER OPERATIONS (DAY 6) ==================== */
            case REQ_CUST_VIEW_BALANCE: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to view balance.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    BalancePayload bal;
                    memset(&bal, 0, sizeof(bal));
                    int status = customer_get_balance(active_user_id, active_account_id, &bal,
                                                      res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &bal, sizeof(BalancePayload));
                        res.data_len = sizeof(BalancePayload);
                    }
                }
                break;
            }

            case REQ_CUST_DEPOSIT: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to deposit funds.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    AmountPayload *ap = (AmountPayload *)req.payload;
                    TransactionResultPayload txn_res;
                    memset(&txn_res, 0, sizeof(txn_res));
                    int status = customer_deposit(active_user_id, active_account_id, ap->amount,
                                                 &txn_res, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &txn_res, sizeof(TransactionResultPayload));
                        res.data_len = sizeof(TransactionResultPayload);
                        printf("[WORKER %d] Deposit of $%.2f to Acc %d completed (Txn ID: %d)\n",
                               pid, ap->amount, active_account_id, txn_res.transaction_id);
                    }
                }
                break;
            }

            case REQ_CUST_WITHDRAW: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to withdraw funds.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    AmountPayload *ap = (AmountPayload *)req.payload;
                    TransactionResultPayload txn_res;
                    memset(&txn_res, 0, sizeof(txn_res));
                    int status = customer_withdraw(active_user_id, active_account_id, ap->amount,
                                                  &txn_res, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &txn_res, sizeof(TransactionResultPayload));
                        res.data_len = sizeof(TransactionResultPayload);
                        printf("[WORKER %d] Withdrawal of $%.2f from Acc %d completed (Txn ID: %d)\n",
                               pid, ap->amount, active_account_id, txn_res.transaction_id);
                    }
                }
                break;
            }

            case REQ_EXIT: {
                if (active_user_id > 0 && session_fd >= 0) {
                    auth_logout(active_user_id, session_fd);
                    active_user_id = 0;
                    active_role = 0;
                    active_account_id = 0;
                    session_fd = -1;
                }
                res.status_code = RESP_OK;
                snprintf(res.message, sizeof(res.message), "Session closed. Goodbye!");
                send_response(client_sock, &res);
                printf("[WORKER %d] Client %s:%d requested exit.\n", pid, client_ip, client_port);
                goto cleanup;
            }

            case 100: { /* Initial connection probe / ping */
                res.status_code = RESP_OK;
                snprintf(res.message, sizeof(res.message), "Server Worker PID %d ready.", pid);
                snprintf(res.payload, sizeof(res.payload), "ACK: Handshake probe successful.");
                res.data_len = strlen(res.payload);
                break;
            }

            default: {
                /* Generic handler for role opcodes in Days 7-11 */
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required for opcode %d.", req.opcode);
                } else {
                    res.status_code = RESP_OK;
                    snprintf(res.message, sizeof(res.message), "Worker PID %d acknowledged opcode %d.", pid, req.opcode);
                    snprintf(res.payload, sizeof(res.payload), "ACK: User ID %d dispatched opcode %d.", active_user_id, req.opcode);
                    res.data_len = strlen(res.payload);
                }
                break;
            }
        }

        if (send_response(client_sock, &res) < 0) {
            printf("[WORKER %d] Failed to send response to %s:%d\n", pid, client_ip, client_port);
            break;
        }
    }

cleanup:
    /* Safety clean-up on exit or abrupt crash */
    if (active_user_id > 0 && session_fd >= 0) {
        auth_logout(active_user_id, session_fd);
    }

    close(client_sock);
    printf("[WORKER %d] Connection with %s:%d closed cleanly. Exiting child process.\n", pid, client_ip, client_port);
    exit(0);
}
