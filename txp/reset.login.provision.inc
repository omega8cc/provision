<?php
/**
 * @file
 * Provision hooks for the provision login-reset command (TXP layer).
 *
 * The frontend's ungated "Reset password" button + the cached login_link both
 * land here. TXP has no one-time login; the nearest native primitive is the
 * password-reset confirmation link (txp_token type password_reset), minted
 * through TXP's OWN Security\Token machinery by the token shim (a separate
 * >= 8.4 process, like the install shim) so the hash scheme is never
 * replicated by hand. The resulting link opens the admin's password-reset
 * confirmation form.
 */

function drush_provision_txp_provision_login_reset() {
  if (!provision_platform_is_txp(d())) {
    return;
  }
  if (d()->type !== 'site') {
    return;
  }
  $link = _provision_txp_mint_login_link();
  if ($link === FALSE) {
    return drush_set_error('PROVISION_TXP_RESET_FAILED',
      dt('Could not mint a password-reset link for !uri.', array('!uri' => d()->uri)));
  }
  drush_set_option('login_link', $link);
  drush_log(dt('Password-reset link minted for !uri.', array('!uri' => d()->uri)), 'ok');
}
