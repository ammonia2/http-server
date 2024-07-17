#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <vector>
#include <regex>

void handleConnection(int, const std::string&);
std::string directory;

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--directory" && i + 1 < argc) {
      directory = argv[++i];
      if (directory.back() != '/') {
        directory += '/';
      }
    }
  }

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  while (true) {
    std::cout << "Waiting for a client to connect...\n";
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
    if (client < 0) {
      std::cerr << "accept failed\n";
      continue;
    }
    std::thread(handleConnection, client, directory).detach();
  }

  close(server_fd);
  return 0;
}

void handleConnection(int client, const std::string& directory) {
  char msg[65536] = {};
  if (recv(client, msg, sizeof(msg)-1, 0) <= 0) {
    std::cerr << "recv failed\n";
    close(client);
    return;
  }

  std::string request(msg);
  std::istringstream requestStream(request);
  std::string method, path, version;
  requestStream >> method >> path >> version;

  bool supportsGzip = false;
  std::regex gzipRegex("gzip");
  std::smatch match;
  std::string headers;
  std::string line;
  while (std::getline(requestStream, line) && line != "\r") {
    headers += line + "\n";
    if (line.find("Accept-Encoding") != std::string::npos && std::regex_search(line, match, gzipRegex)) {
      supportsGzip = true;
    }
  }

  if (method == "POST" && path.find("/files/") == 0) {
    std::string filename = path.substr(7);
    std::string filepath = directory + filename;

    size_t contentLengthPos = headers.find("Content-Length: ");
    if (contentLengthPos != std::string::npos) {
      size_t start = contentLengthPos + 16;
      size_t end = headers.find("\r\n", start);
      int contentLength = std::stoi(headers.substr(start, end - start));

      std::string requestBody = request.substr(request.find("\r\n\r\n") + 4, contentLength);

      std::ofstream file(filepath, std::ios::binary);
      if (file.is_open()) {
        file.write(requestBody.c_str(), contentLength);
        file.close();
        std::ostringstream response;
        response << "HTTP/1.1 201 Created\r\n\r\n";
        send(client, response.str().c_str(), response.str().length(), 0);
      } else {
        std::ostringstream response;
        response << "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        send(client, response.str().c_str(), response.str().length(), 0);
      }
    } else {
      std::ostringstream response;
      response << "HTTP/1.1 400 Bad Request\r\n\r\n";
      send(client, response.str().c_str(), response.str().length(), 0);
    }
  } else if (method == "GET" && path.find("/files/") == 0) {
    std::string filename = path.substr(7);
    std::string filepath = directory + filename;

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
      std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);

      std::vector<char> buffer(size);
      if (file.read(buffer.data(), size)) {
        std::ostringstream header;
        header << "HTTP/1.1 200 OK\r\n"
               << "Content-Type: text/plain\r\n"
               << "Content-Length: " << size << "\r\n";
        if (supportsGzip) {
          header << "Content-Encoding: gzip\r\n";
        }
        header << "\r\n";
        send(client, header.str().c_str(), header.str().length(), 0);
        send(client, buffer.data(), size, 0);
      }
      file.close();
    } else {
      std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
      send(client, response.c_str(), response.length(), 0);
    }
  } else if (path == "/echo") {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/plain\r\n"
             << "Content-Length: " << path.length() << "\r\n\r\n"
             << path;
    send(client, response.str().c_str(), response.str().length(), 0);
  } else if (path == "/user-agent") {
    std::string userAgent = headers.substr(headers.find("User-Agent: ") + 12);
    userAgent = userAgent.substr(0, userAgent.find("\r\n"));
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: text/plain\r\n"
             << "Content-Length: " << userAgent.length() << "\r\n\r\n"
             << userAgent;
    send(client, response.str().c_str(), response.str().length(), 0);
  } else {
    std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
    send(client, response.c_str(), response.length(), 0);
  }

  close(client);
  std::cout << "Client connected" << std::endl;
}
