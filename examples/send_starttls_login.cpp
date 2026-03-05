#include <smtp_client/smtp_client.hpp>
#include <iostream>

int main()
{
  smtp_client::Options opt;
  opt.host = "smtp.example.com";
  opt.port = 587;

  opt.use_starttls = true;

  opt.use_auth = true;
  opt.username = "smtp_username";
  opt.password = "smtp_password";

  smtp_client::Email mail;
  mail.from = "sender@example.com";
  mail.to = {"receiver@example.com"};
  mail.subject = "SMTP STARTTLS test";
  mail.text_body = "Hello via STARTTLS (AUTH LOGIN).";

  auto r = smtp_client::send(opt, mail);

  if (!r.ok)
  {
    std::cerr << "SMTP error: " << r.error << "\n";
    if (r.last_code)
      std::cerr << "Last code: " << r.last_code << "\n";
    if (!r.last_message.empty())
      std::cerr << r.last_message << "\n";
    return 1;
  }

  std::cout << "Email sent\n";
  return 0;
}
