/**
 * @file smtp_client.hpp
 * @brief Minimal SMTP client for sending emails over TCP with optional STARTTLS.
 *
 * Features:
 * - Plain SMTP over TCP
 * - Optional STARTTLS upgrade (OpenSSL) if enabled
 * - AUTH LOGIN (basic auth) and no-auth mode
 * - Minimal MIME builder for text/plain and text/html
 * - Deterministic protocol handling
 *
 * Notes:
 * - This is intentionally minimal. It does not implement:
 *   - OAuth2 (XOAUTH2)
 *   - DKIM signing
 *   - SMTPUTF8 / IDN
 *   - Full MIME multipart attachments
 * - For TLS: define SMTP_CLIENT_ENABLE_OPENSSL and link OpenSSL.
 *
 * Header-only. C++17+.
 */

#ifndef SMTP_CLIENT_SMTP_CLIENT_HPP
#define SMTP_CLIENT_SMTP_CLIENT_HPP

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <chrono>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace smtp_client
{
  struct Options
  {
    std::string host;    // e.g. "smtp.gmail.com"
    uint16_t port = 587; // 25, 587 (STARTTLS), 465 (implicit TLS not implemented here)
    int timeout_ms = 5000;

    // EHLO name (defaults to "localhost" if empty)
    std::string helo_name;

    // STARTTLS upgrade (recommended on 587). Requires OpenSSL enabled.
    bool use_starttls = true;

    // AUTH LOGIN
    bool use_auth = false;
    std::string username;
    std::string password;

    // If true, tolerate servers that don't advertise STARTTLS (will continue plaintext).
    bool allow_plaintext_fallback = false;
  };

  struct Email
  {
    std::string from;             // sender email
    std::vector<std::string> to;  // recipients
    std::vector<std::string> cc;  // optional
    std::vector<std::string> bcc; // optional
    std::string subject;

    // If both provided, we'll send multipart/alternative.
    std::string text_body;
    std::string html_body;

    // Optional extra headers (raw "Key: Value")
    std::vector<std::string> headers;
  };

  struct Result
  {
    bool ok = false;
    int last_code = 0;
    std::string last_message;
    std::string error;
  };

  /**
   * @brief Send an email using SMTP (TCP), optionally upgrading with STARTTLS.
   */
  inline Result send(const Options &opt, const Email &mail);

  namespace detail
  {
#if defined(_WIN32)
    struct WsaGuard
    {
      bool ok = false;
      WsaGuard()
      {
        WSADATA wsa{};
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
      }
      ~WsaGuard()
      {
        if (ok)
          WSACleanup();
      }
    };

    inline void close_fd(int fd)
    {
      if (fd != INVALID_SOCKET)
        ::closesocket((SOCKET)fd);
    }
#else
    inline void close_fd(int fd)
    {
      if (fd >= 0)
        ::close(fd);
    }
#endif

    inline std::string trim_crlf(std::string s)
    {
      while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
      return s;
    }

    inline std::string to_lower(std::string s)
    {
      for (auto &c : s)
        c = (char)std::tolower((unsigned char)c);
      return s;
    }

    inline std::string join_list(const std::vector<std::string> &xs, const std::string &sep)
    {
      std::string out;
      for (size_t i = 0; i < xs.size(); ++i)
      {
        if (i)
          out += sep;
        out += xs[i];
      }
      return out;
    }

    inline std::string base64_encode(const std::string &in)
    {
      static const char *tbl =
          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::string out;
      out.reserve(((in.size() + 2) / 3) * 4);

      size_t i = 0;
      while (i + 2 < in.size())
      {
        uint32_t v = ((uint32_t)(unsigned char)in[i] << 16) |
                     ((uint32_t)(unsigned char)in[i + 1] << 8) |
                     ((uint32_t)(unsigned char)in[i + 2]);
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(tbl[(v >> 6) & 0x3F]);
        out.push_back(tbl[v & 0x3F]);
        i += 3;
      }

      if (i < in.size())
      {
        uint32_t v = (uint32_t)(unsigned char)in[i] << 16;
        out.push_back(tbl[(v >> 18) & 0x3F]);

        if (i + 1 < in.size())
        {
          v |= (uint32_t)(unsigned char)in[i + 1] << 8;
          out.push_back(tbl[(v >> 12) & 0x3F]);
          out.push_back(tbl[(v >> 6) & 0x3F]);
          out.push_back('=');
        }
        else
        {
          out.push_back(tbl[(v >> 12) & 0x3F]);
          out.push_back('=');
          out.push_back('=');
        }
      }

      return out;
    }

    inline std::string make_boundary()
    {
      // deterministic-ish boundary using time ticks + address
      auto now = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
      std::ostringstream oss;
      oss << "smtp_client_boundary_" << std::hex << now;
      return oss.str();
    }

    inline std::string rfc5322_date()
    {
      // Minimal: leave empty to avoid locale/timezone handling.
      // Many SMTP servers will accept without Date, but it's better to provide in higher layers.
      return {};
    }

    inline std::string dot_stuff(const std::string &s)
    {
      // If a line starts with '.', prepend another '.'
      std::string out;
      out.reserve(s.size() + 16);

      bool line_start = true;
      for (char c : s)
      {
        if (line_start && c == '.')
          out.push_back('.');
        out.push_back(c);
        if (c == '\n')
          line_start = true;
        else if (c != '\r')
          line_start = false;
      }
      return out;
    }

    inline std::string ensure_crlf(std::string s)
    {
      // Normalize newlines to CRLF.
      std::string out;
      out.reserve(s.size() + 16);

      for (size_t i = 0; i < s.size(); ++i)
      {
        char c = s[i];
        if (c == '\r')
        {
          out.push_back('\r');
          if (i + 1 < s.size() && s[i + 1] == '\n')
          {
            out.push_back('\n');
            ++i;
          }
          else
          {
            out.push_back('\n');
          }
        }
        else if (c == '\n')
        {
          out.push_back('\r');
          out.push_back('\n');
        }
        else
        {
          out.push_back(c);
        }
      }

      return out;
    }

    inline std::string build_message(const Email &m)
    {
      if (m.from.empty())
        throw std::runtime_error("smtp_client: missing from");
      if (m.to.empty() && m.cc.empty() && m.bcc.empty())
        throw std::runtime_error("smtp_client: missing recipients");

      std::ostringstream o;

      o << "From: " << m.from << "\r\n";
      if (!m.to.empty())
        o << "To: " << join_list(m.to, ", ") << "\r\n";
      if (!m.cc.empty())
        o << "Cc: " << join_list(m.cc, ", ") << "\r\n";
      o << "Subject: " << m.subject << "\r\n";
      o << "MIME-Version: 1.0\r\n";

      for (const auto &h : m.headers)
      {
        if (!h.empty())
          o << h << "\r\n";
      }

      const bool has_text = !m.text_body.empty();
      const bool has_html = !m.html_body.empty();

      if (has_text && has_html)
      {
        const std::string boundary = make_boundary();
        o << "Content-Type: multipart/alternative; boundary=\"" << boundary << "\"\r\n";
        o << "\r\n";

        o << "--" << boundary << "\r\n";
        o << "Content-Type: text/plain; charset=\"utf-8\"\r\n";
        o << "Content-Transfer-Encoding: 8bit\r\n\r\n";
        o << ensure_crlf(m.text_body) << "\r\n";

        o << "--" << boundary << "\r\n";
        o << "Content-Type: text/html; charset=\"utf-8\"\r\n";
        o << "Content-Transfer-Encoding: 8bit\r\n\r\n";
        o << ensure_crlf(m.html_body) << "\r\n";

        o << "--" << boundary << "--\r\n";
      }
      else if (has_html)
      {
        o << "Content-Type: text/html; charset=\"utf-8\"\r\n";
        o << "Content-Transfer-Encoding: 8bit\r\n";
        o << "\r\n";
        o << ensure_crlf(m.html_body) << "\r\n";
      }
      else
      {
        o << "Content-Type: text/plain; charset=\"utf-8\"\r\n";
        o << "Content-Transfer-Encoding: 8bit\r\n";
        o << "\r\n";
        o << ensure_crlf(m.text_body) << "\r\n";
      }

      return o.str();
    }

    inline int connect_tcp(const std::string &host, uint16_t port)
    {
      if (host.empty())
        return -1;

      addrinfo hints{};
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;

      addrinfo *res = nullptr;
      const std::string port_s = std::to_string(port);

      if (::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res) != 0)
        return -1;

      int sock = -1;
      for (addrinfo *p = res; p; p = p->ai_next)
      {
#if defined(_WIN32)
        SOCKET s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET)
          continue;
        if (::connect(s, p->ai_addr, (int)p->ai_addrlen) == 0)
        {
          sock = (int)s;
          break;
        }
        ::closesocket(s);
#else
        int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0)
          continue;
        if (::connect(s, p->ai_addr, p->ai_addrlen) == 0)
        {
          sock = s;
          break;
        }
        ::close(s);
#endif
      }

      ::freeaddrinfo(res);
      return sock;
    }

    // ----------------------------
    // Transport abstraction: plain or TLS
    // ----------------------------

    struct Transport
    {
      int fd = -1;

#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
      SSL_CTX *ctx = nullptr;
      SSL *ssl = nullptr;
      bool tls = false;
#endif

      ~Transport()
      {
#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
        if (ssl)
          SSL_free(ssl);
        if (ctx)
          SSL_CTX_free(ctx);
#endif
        close_fd(fd);
      }

      bool write_all(const std::string &s)
      {
        const char *p = s.data();
        size_t left = s.size();

        while (left > 0)
        {
          int n = 0;

#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
          if (tls)
            n = SSL_write(ssl, p, (int)left);
          else
#endif
#if defined(_WIN32)
            n = ::send((SOCKET)fd, p, (int)left, 0);
#else
          n = (int)::send(fd, p, left, 0);
#endif

          if (n <= 0)
            return false;

          p += n;
          left -= (size_t)n;
        }

        return true;
      }

      bool read_line(std::string &line)
      {
        line.clear();
        char c = 0;

        while (true)
        {
          int n = 0;

#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
          if (tls)
            n = SSL_read(ssl, &c, 1);
          else
#endif
#if defined(_WIN32)
            n = ::recv((SOCKET)fd, &c, 1, 0);
#else
          n = (int)::recv(fd, &c, 1, 0);
#endif

          if (n <= 0)
            return false;

          line.push_back(c);

          if (line.size() >= 2 &&
              line[line.size() - 2] == '\r' &&
              line[line.size() - 1] == '\n')
            return true;

          if (line.size() > 8192)
            return false;
        }
      }

      bool starttls(const std::string &server_name)
      {
#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
        SSL_library_init();
        SSL_load_error_strings();

        const SSL_METHOD *method = TLS_client_method();
        ctx = SSL_CTX_new(method);
        if (!ctx)
          return false;

        ssl = SSL_new(ctx);
        if (!ssl)
          return false;

        SSL_set_fd(ssl, fd);

        if (!server_name.empty())
          SSL_set_tlsext_host_name(ssl, server_name.c_str());

        if (SSL_connect(ssl) != 1)
          return false;

        tls = true;
        return true;
#else
        (void)server_name;
        return false;
#endif
      }
    };

    inline bool read_smtp_reply(Transport &t, int &code, std::string &msg)
    {
      // Reads multiline replies:
      // 250-First line
      // 250-Second line
      // 250 Last line
      msg.clear();
      code = 0;

      std::string line;
      if (!t.read_line(line))
        return false;

      line = trim_crlf(line);
      if (line.size() < 3)
        return false;

      code = std::atoi(line.substr(0, 3).c_str());
      msg += line;

      if (line.size() >= 4 && line[3] == '-')
      {
        while (true)
        {
          if (!t.read_line(line))
            return false;
          line = trim_crlf(line);
          msg += "\n";
          msg += line;
          if (line.size() >= 4 && std::atoi(line.substr(0, 3).c_str()) == code && line[3] == ' ')
            break;
        }
      }

      return true;
    }

    inline bool send_cmd(Transport &t, const std::string &cmd, int &code, std::string &msg)
    {
      if (!t.write_all(cmd))
        return false;
      if (!t.write_all("\r\n"))
        return false;
      return read_smtp_reply(t, code, msg);
    }

    inline bool code_ok(int code)
    {
      // 2xx or 3xx
      return (code >= 200 && code < 400);
    }

    inline std::string pick_helo_name(const Options &opt)
    {
      if (!opt.helo_name.empty())
        return opt.helo_name;
      return "localhost";
    }

    inline bool contains_starttls(const std::string &ehlo_reply)
    {
      // naive: check "STARTTLS" token
      return to_lower(ehlo_reply).find("starttls") != std::string::npos;
    }

    inline bool auth_login(Transport &t, const Options &opt, int &code, std::string &msg)
    {
      if (!send_cmd(t, "AUTH LOGIN", code, msg))
        return false;
      if (code != 334)
        return false;

      if (!send_cmd(t, base64_encode(opt.username), code, msg))
        return false;
      if (code != 334)
        return false;

      if (!send_cmd(t, base64_encode(opt.password), code, msg))
        return false;
      return code_ok(code);
    }

    inline bool mail_from(Transport &t, const std::string &from, int &code, std::string &msg)
    {
      return send_cmd(t, "MAIL FROM:<" + from + ">", code, msg) && code_ok(code);
    }

    inline bool rcpt_to(Transport &t, const std::string &to, int &code, std::string &msg)
    {
      return send_cmd(t, "RCPT TO:<" + to + ">", code, msg) && code_ok(code);
    }

    inline bool data(Transport &t, const std::string &payload, int &code, std::string &msg)
    {
      if (!send_cmd(t, "DATA", code, msg))
        return false;
      if (code != 354)
        return false;

      // dot-stuff and terminate with <CRLF>.<CRLF>
      std::string body = dot_stuff(payload);
      body = ensure_crlf(body);

      if (!t.write_all(body))
        return false;
      if (!t.write_all("\r\n.\r\n"))
        return false;

      if (!read_smtp_reply(t, code, msg))
        return false;
      return code_ok(code);
    }

  } // namespace detail

  inline Result send(const Options &opt, const Email &mail)
  {
    Result r{};

#if defined(_WIN32)
    detail::WsaGuard wsa;
    if (!wsa.ok)
    {
      r.error = "smtp_client: WSAStartup failed";
      return r;
    }
#endif

    if (opt.host.empty())
    {
      r.error = "smtp_client: missing host";
      return r;
    }

    int fd = detail::connect_tcp(opt.host, opt.port);
    if (fd < 0)
    {
      r.error = "smtp_client: connect failed";
      return r;
    }

    detail::Transport t;
    t.fd = fd;

    int code = 0;
    std::string msg;

    // 220 greeting
    if (!detail::read_smtp_reply(t, code, msg))
    {
      r.error = "smtp_client: failed to read greeting";
      return r;
    }
    r.last_code = code;
    r.last_message = msg;
    if (!detail::code_ok(code))
    {
      r.error = "smtp_client: server rejected greeting";
      return r;
    }

    // EHLO
    const std::string helo = detail::pick_helo_name(opt);
    if (!detail::send_cmd(t, "EHLO " + helo, code, msg))
    {
      r.error = "smtp_client: EHLO failed";
      return r;
    }
    r.last_code = code;
    r.last_message = msg;

    bool starttls_advertised = detail::contains_starttls(msg);

    // STARTTLS
    if (opt.use_starttls)
    {
#if defined(SMTP_CLIENT_ENABLE_OPENSSL)
      if (!starttls_advertised && !opt.allow_plaintext_fallback)
      {
        r.error = "smtp_client: STARTTLS not supported by server";
        return r;
      }

      if (starttls_advertised)
      {
        if (!detail::send_cmd(t, "STARTTLS", code, msg))
        {
          r.error = "smtp_client: STARTTLS command failed";
          return r;
        }
        r.last_code = code;
        r.last_message = msg;

        if (code != 220)
        {
          r.error = "smtp_client: STARTTLS rejected";
          return r;
        }

        if (!t.starttls(opt.host))
        {
          r.error = "smtp_client: TLS handshake failed";
          return r;
        }

        // EHLO again after TLS
        if (!detail::send_cmd(t, "EHLO " + helo, code, msg))
        {
          r.error = "smtp_client: EHLO after STARTTLS failed";
          return r;
        }
        r.last_code = code;
        r.last_message = msg;
      }
#else
      r.error = "smtp_client: STARTTLS requested but OpenSSL is not enabled (define SMTP_CLIENT_ENABLE_OPENSSL)";
      return r;
#endif
    }

    // AUTH
    if (opt.use_auth)
    {
      if (opt.username.empty() || opt.password.empty())
      {
        r.error = "smtp_client: missing username/password";
        return r;
      }

      if (!detail::auth_login(t, opt, code, msg))
      {
        r.last_code = code;
        r.last_message = msg;
        r.error = "smtp_client: AUTH LOGIN failed";
        return r;
      }
      r.last_code = code;
      r.last_message = msg;
    }

    // MAIL FROM / RCPT TO
    if (!detail::mail_from(t, mail.from, code, msg))
    {
      r.last_code = code;
      r.last_message = msg;
      r.error = "smtp_client: MAIL FROM failed";
      return r;
    }

    auto rcpt_all = mail.to;
    rcpt_all.insert(rcpt_all.end(), mail.cc.begin(), mail.cc.end());
    rcpt_all.insert(rcpt_all.end(), mail.bcc.begin(), mail.bcc.end());

    for (const auto &to : rcpt_all)
    {
      if (!detail::rcpt_to(t, to, code, msg))
      {
        r.last_code = code;
        r.last_message = msg;
        r.error = "smtp_client: RCPT TO failed";
        return r;
      }
    }

    // DATA
    std::string payload;
    try
    {
      payload = detail::build_message(mail);
    }
    catch (const std::exception &e)
    {
      r.error = e.what();
      return r;
    }

    if (!detail::data(t, payload, code, msg))
    {
      r.last_code = code;
      r.last_message = msg;
      r.error = "smtp_client: DATA failed";
      return r;
    }

    // QUIT
    (void)detail::send_cmd(t, "QUIT", code, msg);

    r.ok = true;
    r.last_code = code;
    r.last_message = msg;
    return r;
  }

} // namespace smtp_client

#endif // SMTP_CLIENT_SMTP_CLIENT_HPP
