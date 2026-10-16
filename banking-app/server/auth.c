#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "server/auth.h"
#include "server/db.h"
#include "server/lock.h"

const char *role_to_string(UserRole role) {
    switch (role) {
        case ROLE_CUSTOMER: return "Customer";
        case ROLE_EMPLOYEE: return "Bank Employee";
        case ROLE_MANAGER:  return "Bank Manager";
        case ROLE_ADMIN:    return "Administrator";
        default:            return "Unknown Role";
    }
}

int auth_login(const LoginPayload *login,
               AuthSuccessPayload *out_auth,
               int *session_fd_out,
               char *msg_out,
               size_t msg_len) {
    if (!login || !out_auth || !session_fd_out) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid request payload.");
        return RESP_ERR_BAD_REQUEST;
    }

    if (strlen(login->username) == 0 || strlen(login->password) == 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Username and password cannot be empty.");
        return RESP_ERR_BAD_REQUEST;
    }

    UserRecord user;
    memset(&user, 0, sizeof(user));
    if (db_read_user_by_username(login->username, &user) != 0) {
        if (msg_out) snprintf(msg_out, msg_len, "User '%s' does not exist.", login->username);
        return RESP_ERR_NOT_FOUND;
    }

    /* Verify password */
    if (strcmp(user.password, login->password) != 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid password. Authentication failed.");
        return RESP_ERR_AUTH_FAILED;
    }

    /* Verify user account is active */
    if (user.is_active == STATUS_INACTIVE) {
        if (msg_out) snprintf(msg_out, msg_len, "User account '%s' is deactivated/frozen.", login->username);
        return RESP_ERR_UNAUTHORIZED;
    }

    /* Single session enforcement via fcntl record lock */
    int session_fd = -1;
    if (acquire_user_session(user.id, &session_fd) < 0) {
        if (msg_out) snprintf(msg_out, msg_len, "User '%s' is already logged in on another terminal.", login->username);
        return RESP_ERR_ALREADY_LOGGED_IN;
    }

    *session_fd_out = session_fd;

    /* Fill response payload */
    memset(out_auth, 0, sizeof(AuthSuccessPayload));
    out_auth->user_id = user.id;
    out_auth->role = user.role;
    out_auth->account_id = user.account_id;
    strncpy(out_auth->username, user.username, sizeof(out_auth->username) - 1);

    if (user.role == ROLE_CUSTOMER && user.account_id > 0) {
        AccountRecord acc;
        if (db_read_account_by_id(user.account_id, &acc) == 0) {
            strncpy(out_auth->customer_name, acc.customer_name, sizeof(out_auth->customer_name) - 1);
            out_auth->balance = acc.balance;
        } else {
            strncpy(out_auth->customer_name, user.username, sizeof(out_auth->customer_name) - 1);
            out_auth->balance = 0.0;
        }
    } else {
        strncpy(out_auth->customer_name, user.username, sizeof(out_auth->customer_name) - 1);
        out_auth->balance = 0.0;
    }

    if (msg_out) {
        snprintf(msg_out, msg_len, "Welcome %s! Logged in successfully as %s.",
                 out_auth->customer_name, role_to_string(user.role));
    }

    return RESP_OK;
}

int auth_logout(int user_id, int session_fd) {
    if (user_id >= USER_ID_BASE && session_fd >= 0) {
        release_user_session(user_id, session_fd);
    }
    return RESP_OK;
}

int auth_change_password(int user_id,
                         const ChangePasswordPayload *payload,
                         char *msg_out,
                         size_t msg_len) {
    if (user_id < USER_ID_BASE || !payload) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid password change request.");
        return RESP_ERR_BAD_REQUEST;
    }

    if (strlen(payload->new_password) < 4) {
        if (msg_out) snprintf(msg_out, msg_len, "New password must be at least 4 characters.");
        return RESP_ERR_BAD_REQUEST;
    }

    UserRecord user;
    if (db_read_user_by_id(user_id, &user) != 0) {
        if (msg_out) snprintf(msg_out, msg_len, "User ID %d not found.", user_id);
        return RESP_ERR_NOT_FOUND;
    }

    /* Verify old password */
    if (strcmp(user.password, payload->old_password) != 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Current password verification failed.");
        return RESP_ERR_AUTH_FAILED;
    }

    /* Update password */
    strncpy(user.password, payload->new_password, sizeof(user.password) - 1);
    if (db_update_user(&user) != 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Failed to update user password in database.");
        return RESP_ERR_INTERNAL;
    }

    if (msg_out) {
        snprintf(msg_out, msg_len, "Password successfully updated for user '%s'.", user.username);
    }

    return RESP_OK;
}
