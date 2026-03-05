# smtp_client

Minimal SMTP client for sending emails.

`smtp_client` provides a small deterministic SMTP client for sending emails
using standard SMTP commands over TCP.

Header-only. No external dependencies by default.

## Download

https://vixcpp.com/registry/pkg/gaspardkirira/smtp_client

## Why smtp_client?

Sending emails is a common requirement in many backend systems:

- application notifications
- password reset emails
- transactional emails
- monitoring alerts
- system automation
- infrastructure tools

Most C++ email solutions rely on heavy frameworks or complex dependencies.

`smtp_client` provides a minimal alternative.

It implements the essential SMTP commands needed to send emails
with a small and predictable codebase.

No framework required.

Just simple SMTP communication.

## Features

- Send emails using SMTP
- Multiple recipients (To, CC, BCC)
- Plain text emails
- HTML emails
- Multipart alternative messages
- Optional SMTP authentication (AUTH LOGIN)
- Optional STARTTLS support
- Deterministic SMTP protocol handling
- Header-only simplicity

No external dependencies required for basic SMTP.

## Installation

### Using Vix Registry

```bash
vix add gaspardkirira/smtp_client
vix deps
```

### Manual

```bash
git clone https://github.com/GaspardKirira/smtp_client.git
```

Add the `include/` directory to your project.

## Dependency

Requires C++17 or newer.

By default the library has no external dependencies.

## Optional TLS support

If you want to use STARTTLS encryption, OpenSSL must be enabled.

Compile with:

- `-DSMTP_CLIENT_ENABLE_OPENSSL=ON`

and link OpenSSL.

Example with CMake:

```cmake
find_package(OpenSSL REQUIRED)

target_compile_definitions(your_target PRIVATE SMTP_CLIENT_ENABLE_OPENSSL)

target_link_libraries(your_target
    OpenSSL::SSL
    OpenSSL::Crypto
)
```

Without OpenSSL, the client works in plain SMTP mode.

## Quick examples

### Send a simple email

```cpp
#include <smtp_client/smtp_client.hpp>

int main()
{
    smtp_client::Options opt;
    opt.host = "smtp.example.com";
    opt.port = 25;

    smtp_client::Email mail;
    mail.from = "sender@example.com";
    mail.to = {"receiver@example.com"};
    mail.subject = "Hello";
    mail.text_body = "Hello from smtp_client.";

    auto result = smtp_client::send(opt, mail);

    if (!result.ok)
        return 1;
}
```

### Send email with authentication

```cpp
#include <smtp_client/smtp_client.hpp>

int main()
{
    smtp_client::Options opt;
    opt.host = "smtp.example.com";
    opt.port = 587;

    opt.use_starttls = true;

    opt.use_auth = true;
    opt.username = "smtp_user";
    opt.password = "smtp_password";

    smtp_client::Email mail;
    mail.from = "sender@example.com";
    mail.to = {"receiver@example.com"};
    mail.subject = "SMTP test";
    mail.text_body = "Email sent using smtp_client.";

    auto result = smtp_client::send(opt, mail);

    if (!result.ok)
        return 1;
}
```

## API overview

Main structures:

- `smtp_client::Options`
- `smtp_client::Email`
- `smtp_client::Result`

Core function:

- `smtp_client::send(options, email)`

## SMTP behavior

The client implements the basic SMTP workflow:

1. CONNECT
2. EHLO
3. STARTTLS (optional)
4. AUTH LOGIN (optional)
5. MAIL FROM
6. RCPT TO
7. DATA
8. QUIT

Supported email formats:

| Format | Support |
|--------|---------|
| Plain text | Yes |
| HTML | Yes |
| Multipart alternative | Yes |

## Limitations

This implementation focuses on simplicity.

Not implemented:

- SMTP attachments
- DKIM signing
- OAuth authentication
- SMTPUTF8
- DNS MX lookup
- SMTP connection pooling

These features can be implemented on top of this layer if needed.

## Complexity

| Operation | Time complexity |
|----------|-----------------|
| SMTP connection | O(1) |
| Email construction | O(n) |
| SMTP communication | O(n) |

Where `n` is the message size.

## Design principles

- Deterministic behavior
- Minimal implementation
- No framework dependencies
- Header-only simplicity
- Predictable SMTP protocol handling

This library focuses strictly on basic SMTP email sending.

If you need:

- email templating
- bulk email systems
- email queues
- advanced SMTP features

Build them on top of this layer.

## Tests

Run:

```bash
vix build
vix test
```

Tests verify:

- email structure validation
- options defaults
- multiple recipients
- message formatting

## License

MIT License
Copyright (c) Gaspard Kirira

