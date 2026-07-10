<?php
/**
 *  @file
 *    Rebuild all the caches on a Backdrop site.
 *
 * Backdrop equivalent of platform/drupal/clear_7.inc. Runs in-process under a
 * FULL bootstrap. backdrop_flush_all_caches() is Backdrop's native full flush.
 */

cache_clear_all('*', 'cache_form', TRUE);
backdrop_flush_all_caches();
drush_log(dt('All caches flushed'));
