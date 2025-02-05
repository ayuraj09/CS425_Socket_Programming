#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 12345
#define BUFFER_SIZE 1024

std::unordered_map<std::string, std::string> users; // username -> password
std::unordered_map<int, std::string> clients;       // socket -> username
std::unordered_map<std::string, std::unordered_set<std::string>> groups; // group -> users
std::mutex clients_mutex;
std::mutex groups_mutex;

void load_users(const std::string &filename) {
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        size_t delimiter = line.find(":");
        if (delimiter != std::string::npos) {
            std::string username = line.substr(0, delimiter);
            std::string password = line.substr(delimiter + 1);
            users[username] = password;
        }
    }
}

void send_message(int client_socket, const std::string &message) {
    send(client_socket, message.c_str(), message.size(), 0);
}

void broadcast_message(const std::string &message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto &[socket, username] : clients) {
        if (socket != sender_socket) {
            send_message(socket, message);
        }
    }
}

void group_broadcast(const std::string &message, int sender_socket, const std::string &group) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    const auto &members = groups[group];
    for (const auto &member : members) {
        for (const auto &[socket, username] : clients) {
            if (username == member && socket != sender_socket) {
                send_message(socket, message);
            }
        }
    }
}

void private_message(const std::string &receiver, const std::string &message, int sender_socket) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto &[socket, username] : clients) {
        if (username == receiver) {
            send_message(socket, message);
            return;
        }
    }
    send_message(sender_socket, "User not found or not connected.\n");
}

void group_message(const std::string &group, const std::string &message, int sender_socket, const std::string &username) {
    std::lock_guard<std::mutex> lock(groups_mutex);
    if (groups.find(group) == groups.end()) {
        send_message(sender_socket, "Group does not exist.\n");
        return;
    }
    if (groups[group].find(username) == groups[group].end()) {
        send_message(sender_socket, "You are not a member of the group.\n");
        return;
    }
    const auto &members = groups[group];
    std::lock_guard<std::mutex> lock_clients(clients_mutex);
    for (const auto &member : members) {
        for (const auto &[socket, uname] : clients) {
            if (uname == member && socket != sender_socket) {
                send_message(socket, message);
            }
        }
    }
}

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];

    send_message(client_socket, "Enter username: ");
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return;
    }
    std::string username(buffer, bytes_received);
    username = username.substr(0, username.find_last_not_of(" \t\n\r") + 1);

    send_message(client_socket, "Enter password: ");
    memset(buffer, 0, BUFFER_SIZE);
    bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_received <= 0) {
        close(client_socket);
        return;
    }
    std::string password(buffer, bytes_received);
    password = password.substr(0, password.find_last_not_of(" \t\n\r") + 1);

    if (users.find(username) == users.end() || users[username] != password) {
        send_message(client_socket, "Authentication failed.\n");
        close(client_socket);
        return;
    }
    {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto &pair : clients) {
        if (pair.second == username) {  // Check if username is already in use
            send_message(client_socket, "You are already logged in from another session.\n");
            close(client_socket);
            return;
        }
    }

    // If not logged in, add to clients
    clients[client_socket] = username;
    }

    send_message(client_socket, "Welcome to the server!\n");
    broadcast_message(username + " has joined the chat.\n", client_socket);

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            break;
        }

        std::string message(buffer, bytes_received);
        size_t start = message.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) {
            send_message(client_socket, "Error: Message cannot be empty.\n");
            continue;
        }
        size_t end = message.find_last_not_of(" \t\n\r");
        message = message.substr(start, end - start + 1);

        if (message == "/exit") {
            break;
        } else if (message.rfind("/broadcast", 0) == 0) {
        if (message.length() == 10) {  // Only "/broadcast" with no message
        send_message(client_socket, "Error: Broadcast message cannot be empty.\n");
    } else {
        std::string content = message.substr(10);
        content = content.substr(content.find_first_not_of(" \t\n\r"));
        if (content.empty()) { 
            send_message(client_socket, "Error: Broadcast message cannot be empty.\n");
        } else {
            broadcast_message(username + ": " + content + "\n", client_socket);
        }
    }
}

 else if (message.rfind("/msg ", 0) == 0) {
            std::string rest = message.substr(5);
            size_t space_pos = rest.find(' ');
            if (space_pos == std::string::npos) {
                send_message(client_socket, "Error: Recipient and message required.\n");
            } else {
                std::string recipient = rest.substr(0, space_pos);
                std::string msg_content = rest.substr(space_pos + 1);
                msg_content = msg_content.substr(msg_content.find_first_not_of(" \t\n\r"));
                if (recipient.empty() || msg_content.empty()) {
                    send_message(client_socket, "Error: Recipient and message cannot be empty.\n");
                } else {
                    std::string private_msg = "[" + username + "]: " + msg_content + "\n";
                    private_message(recipient, private_msg, client_socket);
                }
            }
        } else if (message.rfind("/group_msg ", 0) == 0) {
    std::string rest = message.substr(11);
    size_t space_pos = rest.find(' ');
    if (space_pos == std::string::npos) {
        send_message(client_socket, "Error: Group name and message required.\n");
    } else {
        std::string group = rest.substr(0, space_pos);
        std::string msg_content = rest.substr(space_pos + 1);
        msg_content = msg_content.substr(msg_content.find_first_not_of(" \t\n\r"));
        if (group.empty() || msg_content.empty()) { // Updated check
            send_message(client_socket, "Error: Group name and message cannot be empty.\n");
        } else {
            std::string group_msg = "[Group " + group + "] :: (" + username + "): " + msg_content + "\n";
            group_message(group, group_msg, client_socket, username);
        }
    }
}
 else if (message.rfind("/create_group", 0) == 0) { // Updated to handle missing space
            if (message.length() == 13) { // Only "/create_group"
                send_message(client_socket, "Error: Group name cannot be empty.\n");
            } else {
                std::string group = message.substr(14);
                group = group.substr(group.find_first_not_of(" \t\n\r"));
                group = group.substr(0, group.find_last_not_of(" \t\n\r") + 1);
                if (group.empty()) {
                    send_message(client_socket, "Error: Group name cannot be empty.\n");
                } else {
                    std::lock_guard<std::mutex> lock(groups_mutex);
                    if (groups.find(group) == groups.end()) {
                        groups[group].insert(username);
                        send_message(client_socket, "Group created successfully.\n");
                    } else {
                        send_message(client_socket, "Group already exists.\n");
                    }
                }
            }
        }else if (message.rfind("/join_group ", 0) == 0) {
            std::string group = message.substr(12);
            group = group.substr(group.find_first_not_of(" \t\n\r"));
            group = group.substr(0, group.find_last_not_of(" \t\n\r") + 1);
            if (group.empty()) {
                send_message(client_socket, "Error: Group name cannot be empty.\n");
            } else {
                std::lock_guard<std::mutex> lock(groups_mutex);
                if (groups.find(group) == groups.end()) {
                    send_message(client_socket, "Group does not exist.\n");
                } else if (groups[group].count(username) > 0) {  // Check if user is already in the group
                    send_message(client_socket, "You are already in this group.\n");
                } else {
                    groups[group].insert(username);
                    send_message(client_socket, "Joined group successfully.\n");
                    group_broadcast(username + " has joined the group " + group + ".\n", client_socket, group);
                }
            }
        }
 else if (message.rfind("/leave_group ", 0) == 0) {
            std::string group = message.substr(13);
            group = group.substr(group.find_first_not_of(" \t\n\r"));
            group = group.substr(0, group.find_last_not_of(" \t\n\r") + 1);
            if (group.empty()) {
                send_message(client_socket, "Error: Group name cannot be empty.\n");
            } else {
                std::lock_guard<std::mutex> lock(groups_mutex);
                if (groups.find(group) != groups.end() && groups[group].count(username) > 0) {
                    groups[group].erase(username);
                    send_message(client_socket, "Left group successfully.\n");
                    if (groups[group].empty()) {
                        groups.erase(group);
                    } else {
                        group_broadcast(username + " has left the group " + group + ".\n", client_socket, group);
                    }
                } else {
                    send_message(client_socket, "Group does not exist or you are not a member.\n");
                }
            }
        } else {
            send_message(client_socket, "Error: Unknown command.\n");
        }
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        clients.erase(client_socket);
    }

    broadcast_message(username + " has left the chat.\n", client_socket);
    close(client_socket);
}

int main() {
    load_users("users.txt");

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Error creating socket." << std::endl;
        return 1;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (sockaddr *)&server_address, sizeof(server_address)) < 0) {
        std::cerr << "Error binding socket." << std::endl;
        return 1;
    }

    if (listen(server_socket, 10) < 0) {
        std::cerr << "Error listening on socket." << std::endl;
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;

    while (true) {
        sockaddr_in client_address;
        socklen_t client_length = sizeof(client_address);
        int client_socket = accept(server_socket, (sockaddr *)&client_address, &client_length);

        if (client_socket < 0) {
            std::cerr << "Error accepting connection." << std::endl;
            continue;
        }

        std::thread(handle_client, client_socket).detach();
    }

    close(server_socket);
    return 0;
}
