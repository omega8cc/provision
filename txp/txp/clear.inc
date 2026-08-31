<?php
/**
 * @file
 * Cache clear for Textpattern: an explicit no-op.
 *
 * TXP keeps no compiled template cache on disk ($txp_parsed is per-request)
 * and ships no cache-clear tooling; there is nothing to clear. The engine
 * exists so generic task flows that include 'clear' stay well-defined.
 */

drush_log(dt('Textpattern has no cache to clear (no-op by design).'), 'ok');
