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
#include <zlib.h>

void handleConnection(int, sockaddr_in &, int);
std::string directory;

std::string compressString(const std::string& str, int compressionLevel = Z_BEST_COMPRESSION) {
    z_stream zs; // Zlib stream
    memset(&zs, 0, sizeof(zs));

    if (deflateInit2(&zs, compressionLevel, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("deflateInit failed.");
    }

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(str.data()));
    zs.avail_in = str.size(); // Input size

    int ret;
    char outbuffer[32768];
    std::string outstring;

    // Compress the data
    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);

        ret = deflate(&zs, Z_FINISH);

        if (outstring.size() < zs.total_out) {
            outstring.append(outbuffer, zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);

    if (ret != Z_STREAM_END) { // Check for errors
        throw std::runtime_error("Exception during zlib compression.");
    }

    return outstring;
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf; // use unit buffering to flush stdout after every output
  std::cerr << std::unitbuf; // use unit buffering to flush stderr after every output
  
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  // std::cout << "Logs from your program will appear here!\n";
  for (int i = 1; i < argc; i++) {
      if (std::string(argv[i]) == "--directory" && i + 1 < argc) {
          directory = argv[++i];
          if (directory.back() != '/') {
              directory += '/'; // Ensure directory path ends with '/'
          }
      }
  }

  // Uncomment this block to pass the first stage
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  
  
  while (true) {
    std::cout << "Waiting for a client to connect...\n";
    int client =accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if (client < 0) {
      std::cerr << "accept failed\n";
      continue;
    }
    std::thread(handleConnection, client, std::ref(client_addr), client_addr_len).detach();
  }
  close(server_fd);

  return 0;
}


void handleConnection(int client, sockaddr_in & client_addr, int client_addr_len) {
  char msg[65536] = {};
  if (recvfrom(client, msg, sizeof(msg)-1, 0, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len) == SO_ERROR){
    std::cerr << "listen failed\n";
  }
  char* getPos = strstr(msg, "/echo/");
  char* userAgentPos = strstr(msg, "User-Agent: ");

  std::string message;

  std::string request(msg);
  std::istringstream requestStream(request);
  std::string method, path;
  requestStream >> method >> path;

  bool supportsGzip = false;
  std::string compressedMessage;
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

  if (getPos != NULL) {
      getPos += strlen("/echo/"); // Move past "/echo/"
      
      char* endOfMessage = strstr(getPos, " HTTP/1.1");
      if (endOfMessage != NULL) {
          message = std::string(getPos, endOfMessage - getPos);
          compressedMessage = supportsGzip? compressString(message): "";
      }
  }
  else {
    if (userAgentPos != NULL) {
      userAgentPos += strlen("User-Agent: ");
      char* endOfHeader = strstr(userAgentPos, "\r\n");
      if (endOfHeader != NULL) {
          message = std::string(userAgentPos, endOfHeader - userAgentPos);
          // compressedMessage = supportsGzip? compressString(message): "";
      }
    }
  }

  if (method == "POST" && path.find("/files/") == 0) {
      std::string filename = path.substr(7);
      std::string filepath = directory + filename;
  
      std::string contentLengthHeader = "Content-Length: ";
      size_t contentLengthPos = request.find(contentLengthHeader);
      if (contentLengthPos != std::string::npos) {
          size_t start = contentLengthPos + contentLengthHeader.length();
          size_t end = request.find("\r\n", start);
          std::string contentLengthStr = request.substr(start, end - start);
          int contentLength = std::stoi(contentLengthStr);
  
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
  }
  else if (method == "GET" && path.find("/files/") == 0) {
      std::string filename = path.substr(7); // Remove "/files/" from the path
      std::string filepath = directory + filename;
  
      std::ifstream file(filepath, std::ios::binary | std::ios::ate);
      if (file.is_open()) {
          std::streamsize size = file.tellg();
          file.seekg(0, std::ios::beg);
  
          std::vector<char> buffer(size);
          if (file.read(buffer.data(), size)) {
              std::ostringstream header;
              header << "HTTP/1.1 200 OK\r\n"<<"Content-Encoding: gzip\r\n"
                     << "Content-Type: application/octet-stream\r\n"
                     << "Content-Length: " << size << "\r\n";
              
              if (supportsGzip) {
                header << "Content-Encoding: gzip\r\n";
              }
              header << "\r\n";

              send(client, header.str().c_str(), header.str().length(), 0);
              if (!supportsGzip) {
                send(client, buffer.data(), size, 0);
              } else {
                std::string compressedData = compressString(std::string(buffer.data(), size));
                send(client, compressedData.c_str(), compressedData.length(), 0);
              }
          }
          file.close();
      } else {
          std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";
          send(client, response.c_str(), response.length(), 0);
      }
  }
  else {
    std::string response = std::string("HTTP/1.1");

    if (strstr(msg, "echo") || strstr(msg, "User-Agent: ")) {
      response += " 200 OK\r\nContent-Type: text/plain\r\n" + ((supportsGzip) ? "Content-Length: " +std::to_string(compressedMessage.length())+ "\r\n"+ std::string("Content-Encoding: gzip\r\n\r\n") + compressedMessage + "/r/n" : "Content-Length: " +std::to_string(message.length()) + "\r\n\r\n" + message + "\r\n");
    }
    else if (msg[5] == ' ') {
      response += " 200 OK\r\n\r\n";
    }
    else {
      response += " 404 Not Found\r\n\r\n";
    }

    send(client, response.c_str(), response.length(), 0);
  }
  std::cout << "Client connected"<<std::endl;

}