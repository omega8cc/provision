<?php $this->root = provision_auto_fix_platform_root($this->root); ?>

<VirtualHost *:<?php print $http_port; ?>>
    <?php if ($this->site_mail) : ?>
      ServerAdmin <?php  print $this->site_mail; ?>
    <?php endif;?>
    DocumentRoot <?php print $this->root; ?>

    ServerName <?php print $this->uri; ?>

    <?php
    if (count($this->aliases)) {
      foreach ($this->aliases as $alias) {
        print "  ServerAlias " . $alias . "\n";
      }
    }
    ?>

    RewriteEngine on

    # Redirect ALL visitors to a configured url.
    # Except for /.well-known/acme-challenge/ to prevent potential problems with Let's Encrypt
    RewriteCond %{REQUEST_URI} '!/.well-known/acme-challenge/'

    # the ? at the end is to remove any query string in the original url
    RewriteRule ^(.*)$ <?php print $this->platform->server->web_disable_url . '/' . $this->uri ?>?

</VirtualHost>
