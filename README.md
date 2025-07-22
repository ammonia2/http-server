**HTTP Server from Scratch in C++**

**Core Features:**
- Multi-threaded client handling
- GET/POST request parsing
- Echo endpoint: /echo/[message]
- File operations: /files/[filename]
- Gzip compression support
- User-Agent parsing

**Supported Endpoints:**
- `/echo/[message]` - Echo service with compression support
- `/files/[filename]` - File upload (POST) and download (GET)
- User-Agent header parsing
- Root path handling

**Usage:**
```bash
./server --directory /path/to/files
```
Server runs on port 4221 with configurable file directory.

**Technical Implementation:**
- Raw socket API (AF_INET, SOCK_STREAM)
- Manual HTTP request/response parsing
- Thread-per-connection model
- Binary file handling with proper Content-Type headers
- Gzip compression with Accept-Encoding negotiation

**Requirements:**
- C++11 or later
- zlib library
- Unix-like system (uses sys/socket.h)
