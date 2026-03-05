/**
 * @file test_basic.cpp
 * @brief Basic tests for smtp_client.
 */

#include <smtp_client/smtp_client.hpp>

#include <cassert>
#include <iostream>

static void test_email_structure()
{
  smtp_client::Email mail;

  mail.from = "sender@example.com";
  mail.to = {"receiver@example.com"};
  mail.subject = "Test message";
  mail.text_body = "Hello from smtp_client test";

  assert(!mail.from.empty());
  assert(!mail.to.empty());
  assert(mail.subject == "Test message");
}

static void test_options_defaults()
{
  smtp_client::Options opt;

  assert(opt.port == 587);
  assert(opt.timeout_ms > 0);
  assert(opt.use_starttls == true);
}

static void test_email_multiple_recipients()
{
  smtp_client::Email mail;

  mail.from = "sender@example.com";
  mail.to = {"a@example.com", "b@example.com"};
  mail.cc = {"c@example.com"};
  mail.bcc = {"d@example.com"};

  assert(mail.to.size() == 2);
  assert(mail.cc.size() == 1);
  assert(mail.bcc.size() == 1);
}

static void test_email_bodies()
{
  smtp_client::Email mail;

  mail.text_body = "plain text body";
  mail.html_body = "<h1>html body</h1>";

  assert(!mail.text_body.empty());
  assert(!mail.html_body.empty());
}

int main()
{
  test_email_structure();
  test_options_defaults();
  test_email_multiple_recipients();
  test_email_bodies();

  std::cout << "smtp_client: all basic tests passed\n";
}
