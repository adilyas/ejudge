/* -*- mode: c -*- */
/* $Id$ */

/* Copyright (C) 2007 Alexander Chernov <cher@ejudge.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "config.h"
#include "settings.h"
#include "ej_types.h"
#include "ej_limits.h"

#include "new-server.h"
#include "contests.h"
#include "misctext.h"
#include "mischtml.h"
#include "xml_utils.h"
#include "l10n.h"

#include <reuse/xalloc.h>

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#if CONF_HAS_LIBINTL - 0 == 1
#include <libintl.h>
#define _(x) gettext(x)
#else
#define _(x) x
#endif
#define __(x) x

#define ARMOR(s)  html_armor_buf(&ab, s)
#define URLARMOR(s)  url_armor_buf(&ab, s)

static int
cgi_param_int(struct http_request_info *phr, const unsigned char *name,
              int *p_val)
{
  const unsigned char *s = 0;
  char *eptr = 0;
  int x;

  if (ns_cgi_param(phr, name, &s) <= 0) return -1;
  errno = 0;
  x = strtol(s, &eptr, 10);
  if (errno || *eptr) return -1;
  if (p_val) *p_val = x;
  return 0;
}

static const unsigned char * const form_row_attrs[]=
{
  " bgcolor=\"#d0d0d0\"",
  " bgcolor=\"#e0e0e0\"",
};

static void
anon_select_contest_page(FILE *fout, struct http_request_info *phr)
{
  struct html_armor_buffer ab = HTML_ARMOR_INITIALIZER;
  unsigned char *cntslist = 0;
  int cntsnum = 0;
  const unsigned char *cl;
  const struct contest_desc *cnts;
  time_t curtime = time(0);
  int row = 0, i;
  const unsigned char *s;
  const unsigned char *login = 0;
  unsigned char bb[1024];

  ns_cgi_param(phr, "login", &login);

  // even don't know about the contest specific settings
  l10n_setlocale(phr->locale_id);
  ns_header(fout, ns_fancy_header, 0, 0, 0, 0, phr->locale_id,
            _("Contest selection"));

  html_start_form(fout, 1, phr->self_url, "");
  fprintf(fout, "<div class=\"user_actions\"><table class=\"menu\"><tr>\n");
  html_hidden(fout, "action", "%d", NEW_SRV_ACTION_CHANGE_LANGUAGE);
  fprintf(fout, "<td class=\"menu\"><div class=\"user_action_item\">%s: ",
          _("language"));
  l10n_html_locale_select(fout, phr->locale_id);
  fprintf(fout, "</div></td>\n");
  fprintf(fout, "<td class=\"menu\"><div class=\"user_action_item\">%s</div></td>\n", ns_submit_button(bb, sizeof(bb), "submit", 0, _("Change Language")));
  fprintf(fout, "</tr></table></div></form>\n");

  fprintf(fout,
          "<div class=\"white_empty_block\">&nbsp;</div>\n"
          "<div class=\"contest_actions\"><table class=\"menu\"><tr>\n");

  fprintf(fout, "<td class=\"menu\"><div class=\"contest_actions_item\">&nbsp;</div></td></tr></table></div>\n");

  fprintf(fout, "%s", ns_fancy_separator);

  fprintf(fout, "<h2>%s</h2>\n", _("Select one of available contests"));

  cntsnum = contests_get_list(&cntslist);
  cl = " class=\"summary\"";
  fprintf(fout, "<table%s><tr>"
          "<td%s>N</td><td%s>%s</td><td%s>%s</td><td%s>%s</td></tr>\n",
          cl, cl, cl, _("Contest name"), cl, _("Registration mode"),
          cl, _("Registration deadline"));
  for (i = 1; i < cntsnum; i++) {
    cnts = 0;
    if (contests_get(i, &cnts) < 0 || !cnts) continue;
    if (cnts->closed) continue;
    if (!contests_check_register_ip_2(cnts, phr->ip, phr->ssl_flag)) continue;
    if (cnts->reg_deadline > 0 && curtime >= cnts->reg_deadline) continue;

    fprintf(fout, "<tr%s><td%s>%d</td>", form_row_attrs[(row++) & 1], cl, i);
    fprintf(fout, "<td%s><a href=\"%s?contest_id=%d", cl, phr->self_url, i);
    if (phr->locale_id > 0) fprintf(fout, "&amp;locale_id=%d", phr->locale_id);
    if (login && *login) fprintf(fout, "&amp;login=%s", URLARMOR(login));
    s = 0;
    if (phr->locale_id == 0 && cnts->name_en) s = cnts->name_en;
    if (!s) s = cnts->name;
    fprintf(fout, "\">%s</a></td>", ARMOR(s));
    if (cnts->autoregister) s = _("open");
    else s = _("moderated");
    fprintf(fout, "<td%s>%s</td>", cl, s);
    if (cnts->reg_deadline > 0) {
      s = xml_unparse_date(cnts->reg_deadline);
    } else {
      s = "&nbsp;";
    }
    fprintf(fout, "<td%s>%s</td>", cl, s);
    fprintf(fout, "</tr>\n");
  }
  fprintf(fout, "</table>\n");

  ns_footer(fout, ns_fancy_footer, 0, phr->locale_id);
  l10n_setlocale(0);

  html_armor_free(&ab);
  xfree(cntslist);
}

static void
register_new_assigned_logins_page(
	FILE *fout,
        struct http_request_info *phr,
        const struct contest_desc *cnts,
        struct contest_extra *extra,
        time_t cur_time)
{
  const unsigned char *email = 0;
  struct html_armor_buffer ab = HTML_ARMOR_INITIALIZER;
  unsigned char bb[1024];
  const unsigned char *head_style = 0, *par_style = 0;
  int item_cnt = 0, allowed_info_edit = 0;
  unsigned char client_url[1024] = { 0 };
  size_t l1, l2;
  int i, j;

  if (cnts->register_head_style && *cnts->register_head_style)
    head_style = cnts->register_head_style;
  if (!head_style) head_style = "h2";
  if (cnts->register_par_style && *cnts->register_par_style)
    par_style = cnts->register_par_style;
  if (!par_style) par_style = "";

  if (!cnts->disable_name) allowed_info_edit = 1;
  for (j = 0; j < CONTEST_LAST_FIELD; j++)
    if (cnts->fields[j])
      allowed_info_edit = 1;
  for (i = 0; i < CONTEST_LAST_MEMBER; i++)
    if (cnts->members[i]->max_count > 0)
      allowed_info_edit = 1;

  if (cnts->team_url)
    snprintf(client_url, sizeof(client_url), "%s", cnts->team_url);
  snprintf(bb, sizeof(bb), "new-register%s", CGI_PROG_SUFFIX);
  l1 = strlen(bb);
  l2 = strlen(phr->self_url);
  if (l1 < l2 && !strcmp(phr->self_url + l2 - l1, bb))
    snprintf(client_url, sizeof(client_url), "%.*snew-client%s",
             (int) (l2 - l1), phr->self_url, CGI_PROG_SUFFIX);

  if (ns_cgi_param(phr, "email", &email) <= 0) email = 0;
  if (!email) email = "";

  l10n_setlocale(phr->locale_id);
  ns_header(fout, extra->header_txt, 0, 0, 0, 0, phr->locale_id,
            "%s [%s]", _("Register a new user"), extra->contest_arm);

  html_start_form(fout, 1, phr->self_url, "");
  html_hidden(fout, "contest_id", "%d", phr->contest_id);
  html_hidden(fout, "next_action", "%d",
              NEW_SRV_ACTION_REGISTER_NEW_AUTOASSIGNED_USER_PAGE);
  fprintf(fout, "<div class=\"user_actions\"><table class=\"menu\"><tr>\n");

  fprintf(fout, "<td class=\"menu\"><div class=\"user_action_item\">e-mail: %s</div></td>", html_input_text(bb, sizeof(bb), "email", 20, "%s", ARMOR(email)));

  fprintf(fout, "<td class=\"menu\"><div class=\"user_action_item\">%s</div></td>", ns_submit_button(bb, sizeof(bb), 0, NEW_SRV_ACTION_REGISTER_NEW_AUTOASSIGNED_USER, _("Register")));

  fprintf(fout, "<td class=\"menu\"><div class=\"user_action_item\">%s: ",
          _("language"));
  l10n_html_locale_select(fout, phr->locale_id);
  fprintf(fout, "</div></td>\n");
  fprintf(fout, "<td class=\"menu\"><div class=\"user_action_item\">%s</div></td>\n", ns_submit_button(bb, sizeof(bb), 0, NEW_SRV_ACTION_CHANGE_LANGUAGE, _("Change Language")));
  fprintf(fout, "</tr></table></div></form>\n");

  fprintf(fout,
          "<div class=\"white_empty_block\">&nbsp;</div>\n"
          "<div class=\"contest_actions\"><table class=\"menu\"><tr>\n");

  // "Forgot password?" "Edit personal info" "Participate in contest/exam"
  if (cnts && cnts->enable_forgot_password && cnts->disable_team_password
      && !cnts->simple_registration) {
    fprintf(fout, "<td class=\"menu\"><div class=\"contest_actions_item\"><a class=\"menu\" href=\"%s?contest_id=%d&amp;locale_id=%d&amp;action=%d\">%s</a></div></td>", phr->self_url, phr->contest_id, phr->locale_id, NEW_SRV_ACTION_FORGOT_PASSWORD_1, _("Forgot password?"));
    item_cnt++;
  }
  if (allowed_info_edit) {
    fprintf(fout, "<td class=\"menu\"><div class=\"contest_actions_item\"><a class=\"menu\" href=\"%s?contest_id=%d&amp;locale_id=%d\">%s</a></div></td>", phr->self_url, phr->contest_id, phr->locale_id, cnts->personal?_("Edit personal info"):_("Edit team info"));
    item_cnt++;
  }
  if (client_url[0]) {
    fprintf(fout, "<td class=\"menu\"><div class=\"contest_actions_item\"><a class=\"menu\" href=\"%s?contest_id=%d&amp;locale_id=%d\">%s</a></div></td>", client_url, phr->contest_id, phr->locale_id, cnts->exam_mode?_("Take the exam"):_("Participate in the contest"));
    item_cnt++;
  }
  if (!item_cnt)
    fprintf(fout, "<td class=\"menu\"><div class=\"contest_actions_item\">&nbsp;</div></td>");
  fprintf(fout, "</tr></table></div>\n");

  fprintf(fout, "%s", extra->separator_txt);

  fprintf(fout, "<%s>%s</%s>\n", head_style, _("Registration rules"),
          head_style);
  fprintf(fout, "<p%s>%s</p>\n", par_style,
          _("Please, enter your valid e-mail address and press the \"Register\" button."));

  fprintf(fout, "<p%s>%s</p>", par_style,
          _("Shortly after that you should receive an e-mail message "
            "with a password to the system. Use this password for the first"
            " login. After the first login you will be able to change the password."));

  fprintf(fout, "<p%s>%s</p>", par_style,
          _("Be careful and type the e-mail address correctly. If you make a mistake, you will not receive a registration e-mail and be unable to complete the registration process."));

  fprintf(fout, "<p%s>%s</p>",
          par_style,
          _("<b>Note</b>, that you must log in "
            "24 hours after the form is filled and submitted, or "
            "your registration will be cancelled!"));

  if (allowed_info_edit || client_url[0]) {
    fprintf(fout, "<p%s>%s", par_style, _("If you are already registered, you may"));
    if (allowed_info_edit)
      fprintf(fout, "<a href=\"%s?contest_id=%d&amp;locale_id=%d&amp;action=?\">%s</a>", phr->self_url, phr->contest_id, phr->locale_id, cnts->personal?_("edit your personal information"):_("edit your team information"));
    if (allowed_info_edit && client_url[0]) fprintf(fout, _(" or"));
    if (client_url[0])
      fprintf(fout, "<a href=\"%s?contest_id=%d&amp;locale_id=%d&amp;action=?\">%s</a>", client_url, phr->contest_id, phr->locale_id, cnts->exam_mode?_("take the exam"):_("participate in the contest"));
    fprintf(fout, ".</p>\n");
  }

  ns_footer(fout, extra->footer_txt, extra->copyright_txt, phr->locale_id);
  l10n_setlocale(0);
  html_armor_free(&ab);
}

typedef void (*reg_action_handler_func_t)(FILE *fout,
        struct http_request_info *phr,
        const struct contest_desc *cnts,
        struct contest_extra *extra,
	time_t cur_time);
static reg_action_handler_func_t action_handlers[NEW_SRV_ACTION_LAST] =
{
  [NEW_SRV_ACTION_REGISTER_NEW_AUTOASSIGNED_USER_PAGE] = register_new_assigned_logins_page,
};

static void
anon_register_pages(FILE *fout, struct http_request_info *phr)
{
  const struct contest_desc *cnts = 0;
  struct contest_extra *extra = 0;
  time_t cur_time = 0;

  // contest_id is reqired
  if (phr->contest_id <= 0 || contests_get(phr->contest_id,&cnts) < 0 || !cnts){
    return anon_select_contest_page(fout, phr);
  }

  // check permissions
  if (cnts->closed ||
      !contests_check_register_ip_2(cnts, phr->ip, phr->ssl_flag)) {
    return ns_html_err_no_perm(fout, phr, 0, "registration is not available");
  }

  // load style stuff
  extra = ns_get_contest_extra(phr->contest_id);
  cur_time = time(0);
  watched_file_update(&extra->header, cnts->team_header_file, cur_time);
  watched_file_update(&extra->footer, cnts->team_footer_file, cur_time);
  watched_file_update(&extra->copyright, cnts->copyright_file, cur_time);
  extra->header_txt = extra->header.text;
  extra->footer_txt = extra->footer.text;
  extra->separator_txt = "";
  extra->copyright_txt = extra->copyright.text;
  if (!extra->header_txt || !extra->footer_txt) {
    extra->header_txt = ns_fancy_header;
    extra->separator_txt = ns_fancy_separator;
    if (extra->copyright_txt) extra->footer_txt = ns_fancy_footer_2;
    else extra->footer_txt = ns_fancy_footer;
  }

  if (extra->contest_arm) xfree(extra->contest_arm);
  if (phr->locale_id == 0 && cnts->name_en) {
    extra->contest_arm = html_armor_string_dup(cnts->name_en);
  } else {
    extra->contest_arm = html_armor_string_dup(cnts->name);
  }

  if (phr->action < 0 || phr->action >= NEW_SRV_ACTION_LAST) phr->action = 0;
  if (action_handlers[phr->action])
    return (*action_handlers[phr->action])(fout, phr, cnts, extra, cur_time);

  if (cnts->assign_logins)
    return register_new_assigned_logins_page(fout, phr, cnts, extra, cur_time);
}

static void
change_locale(FILE *fout, struct http_request_info *phr)
{
  int next_action = 0;
  const unsigned char *sep = "?";
  struct html_armor_buffer ab = HTML_ARMOR_INITIALIZER;
  const unsigned char *s = 0;

  cgi_param_int(phr, "next_action", &next_action);
  if (next_action < 0 || next_action >= NEW_SRV_ACTION_LAST) next_action = 0;

  // SID, contest_id, login are passed "as is"
  // next_action is passed as action
  if (phr->session_id) {
    // FIXME: update the session

    fprintf(fout, "Content-Type: text/html; charset=%s\nCache-Control: no-cache\nPragma: no-cache\nLocation: %s?SID=%016llx", EJUDGE_CHARSET, phr->self_url,
            phr->session_id);
    if (next_action > 0) fprintf(fout, "&action=%d", next_action);
    fprintf(fout, "\n\n");
    return;
  }

  fprintf(fout, "Content-Type: text/html; charset=%s\nCache-Control: no-cache\nPragma: no-cache\nLocation: %s", EJUDGE_CHARSET, phr->self_url);
  if (phr->contest_id > 0) {
    fprintf(fout, "%scontest_id=%d", sep, phr->contest_id);
    sep = "&";
  }
  s = 0;
  if (ns_cgi_param(phr, "login", &s) > 0 && s && *s) {
    fprintf(fout, "%slogin=%s", sep, URLARMOR(s));
    sep = "&";
  }
  s = 0;
  if (ns_cgi_param(phr, "email", &s) > 0 && s && *s) {
    fprintf(fout, "%semail=%s", sep, URLARMOR(s));
    sep = "&";
  }
  if (phr->locale_id > 0) {
    fprintf(fout, "%slocale_id=%d", sep, phr->locale_id);
    sep = "&";
  }
  if (next_action > 0) {
    fprintf(fout, "%saction=%d", sep, next_action);
    sep = "&";
  }
  fprintf(fout, "\n\n");

  html_armor_free(&ab);
}

void
ns_register_pages(FILE *fout, struct http_request_info *phr)
{
  if (phr->action == NEW_SRV_ACTION_CHANGE_LANGUAGE)
    return change_locale(fout, phr);

  if (!phr->session_id) return anon_register_pages(fout, phr);

  // check session
  // get the contest_id and load the contest style stuff
}

/*
 * Local variables:
 *  compile-command: "make"
 *  c-font-lock-extra-types: ("\\sw+_t" "FILE" "va_list")
 * End:
 */
