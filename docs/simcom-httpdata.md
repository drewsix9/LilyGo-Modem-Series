# SIMCOM A76XX – Using `AT+HTTPDATA` for HTTP/HTTPS POST

This guide explains how to use the **`AT+HTTPDATA` AT command** on SIMCOM A76XX LTE modules to send HTTP/HTTPS POST requests.

The command is part of the SIMCOM HTTP(S) AT command set used to interact with web servers over LTE.

A76XX Series_HTTP(S)\_Applicatio…

---

# 1. Overview

The SIMCOM HTTP(S) workflow involves several AT commands:

| Command         | Description          |
| --------------- | -------------------- |
| `AT+HTTPINIT`   | Start HTTP service   |
| `AT+HTTPPARA`   | Set HTTP parameters  |
| `AT+HTTPDATA`   | Input HTTP POST data |
| `AT+HTTPACTION` | Execute HTTP method  |
| `AT+HTTPREAD`   | Read HTTP response   |
| `AT+HTTPTERM`   | Stop HTTP service    |

These commands form the typical HTTP request lifecycle.

A76XX Series_HTTP(S)\_Applicatio…

---

# 2. Purpose of `AT+HTTPDATA`

`AT+HTTPDATA` is used to **send POST request payload data** before executing the HTTP request.

It allows the device to upload data such as:

- JSON payload
- form data
- binary data (e.g., image bytes)

The data is buffered inside the modem until the request is executed.

---

# 3. Syntax

AT+HTTPDATA=<data_length>,<timeout>

### Parameters

| Parameter     | Description                         |
| ------------- | ----------------------------------- |
| `data_length` | Number of bytes to send             |
| `timeout`     | Time (ms) allowed to input the data |

---

# 4. Command Flow

The basic HTTP POST sequence is:

AT+HTTPINIT  
AT+HTTPPARA="URL","http://example.com/api"  
AT+HTTPDATA=<length>,<timeout>  
<data payload>  
AT+HTTPACTION=1  
AT+HTTPREAD  
AT+HTTPTERM

This sequence initializes HTTP, sends data, performs POST, reads the response, and closes the session.

A76XX Series_HTTP(S)\_Applicatio…

---

# 5. Example: HTTP POST

Example taken from the SIMCOM documentation.

### Step 1 — Initialize HTTP service

AT+HTTPINIT  
OK

Starts the HTTP service and activates the PDP context.

---

### Step 2 — Set target URL

AT+HTTPPARA="URL","http://api.example.com/post"  
OK

Defines the HTTP server endpoint.

---

### Step 3 — Send POST data

AT+HTTPDATA=18,1000  
DOWNLOAD  
Message=helloworld  
OK

Explanation:

| Element    | Meaning                        |
| ---------- | ------------------------------ |
| `18`       | length of data                 |
| `1000`     | timeout in milliseconds        |
| `DOWNLOAD` | modem ready to receive payload |

The modem will wait for the exact number of bytes specified.

---

### Step 4 — Execute POST request

AT+HTTPACTION=1  
OK  
+HTTPACTION: 1,500,30

Response format:

+HTTPACTION: <method>,<status_code>,<response_length>

Example values:

| Value | Meaning          |
| ----- | ---------------- |
| `1`   | POST request     |
| `500` | HTTP status code |
| `30`  | response length  |

---

### Step 5 — Read HTTP response

AT+HTTPREAD=0,30  
OK  
+HTTPREAD: 30  
Request format is invalid

Reads server response body.

---

### Step 6 — Terminate HTTP session

AT+HTTPTERM  
OK

Stops HTTP service and releases resources.

---

# 6. HTTPS Example

HTTPS requests follow the same process.

Example:

AT+HTTPINIT  
AT+HTTPPARA="URL","https://example.com/api"  
AT+HTTPDATA=465,1000  
DOWNLOAD  
<JSON payload>  
OK  
AT+HTTPACTION=1  
+HTTPACTION: 1,200,2

The HTTP response can then be read with:

AT+HTTPREAD

HTTPS is supported by the same AT command sequence.

A76XX Series_HTTP(S)\_Applicatio…

---

# 7. Response Handling

The modem returns:

+HTTPACTION: <method>,<status>,<length>

Example:

+HTTPACTION: 1,200,120

| Field  | Meaning                           |
| ------ | --------------------------------- |
| method | HTTP method (0=GET,1=POST,2=HEAD) |
| status | HTTP status code                  |
| length | response body length              |

Common status codes:

| Code | Meaning                  |
| ---- | ------------------------ |
| 200  | OK                       |
| 201  | Created                  |
| 400  | Bad Request              |
| 401  | Unauthorized             |
| 404  | Not Found                |
| 413  | Request Entity Too Large |
| 500  | Internal Server Error    |

Error codes are summarized in the document appendix.

A76XX Series_HTTP(S)\_Applicatio…

---

# 8. Important Notes

### Exact byte length required

The number of bytes sent must match `data_length`.

Otherwise the modem will return an error.

---

### Binary uploads supported

`AT+HTTPDATA` can send binary data such as:

- JPEG images
- files
- sensor logs

As long as the byte count is correct.

---

### Timeout handling

If data is not sent before the timeout expires:

ERROR

---

### Network prerequisites

Before using HTTP commands ensure:

AT+CSQ  
AT+CREG?  
AT+CGREG?  
AT+CPSI?

to confirm network registration.

A76XX Series_HTTP(S)\_Applicatio…

---

# 9. Typical IoT Use Case

Example IoT workflow:

1. Connect to LTE network
2. Initialize HTTP
3. Set API endpoint
4. Upload JSON or image
5. Execute POST
6. Read response
7. Close session

This pattern is commonly used in embedded IoT systems using SIMCOM LTE modules.

---

# 10. Example for IoT Telemetry

Example payload:

AT+HTTPDATA=72,1000  
DOWNLOAD  
{"trap_id":"TRAP01","gps_lat":9.77,"gps_lon":124.47,"battery":3.9}  
OK

Then:

AT+HTTPACTION=1

---

# 11. Example for Image Upload

Binary upload using JPEG data:

AT+HTTPDATA=40960,10000  
DOWNLOAD  
<JPEG binary bytes>  
OK  
AT+HTTPACTION=1

This allows sending images from devices like ESP32-CAM.

---

# 12. Closing HTTP Service

Always terminate the HTTP service:

AT+HTTPTERM

This releases the modem resources.

---

# Summary

`AT+HTTPDATA` is used to:

- upload POST payloads
- send JSON or binary data
- prepare request body before executing HTTP POST

Full request flow:

HTTPINIT  
HTTPPARA  
HTTPDATA  
HTTPACTION  
HTTPREAD  
HTTPTERM

---

If you'd like, I can also write a **second markdown doc specifically for your CRB trap system**:

docs/simcom-image-upload.md

showing **how to upload ESP32-CAM JPEG → Supabase Edge Function using AT+HTTPDATA**, which is the **exact flow your thesis will need.**

---
