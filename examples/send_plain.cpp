#include <smtp_client/smtp_client.hpp>
#include <iostream>

int main()
{
  smtp_client::Options opt;
  opt.host = "smtp.example.com";
  opt.port = 25;
  opt.use_starttls = false;
  opt.use_auth = false;

  smtp_client::Email mail;
  mail.from = "sender@example.com";
  mail.to = {"receiver@example.com"};
  mail.subject = "Hello";
  mail.text_body = "Hello from smtp_client.";

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
