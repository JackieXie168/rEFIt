<?php
// $Id$
?>

<p id="trail">
Trail: <a href="/">Home</a> &gt;
<?php

if ($section == "info" && !$index_html) {
?>
<a href="/info/">Information Repository</a> &gt;
<?php
}
if ($section == "doc" && !$index_html) {
?>
<a href="/doc/">Documentation</a> &gt;
<?php
}
if ($section == "help" && !$index_html) {
?>
<a href="/help/">Troubleshooting</a> &gt;
<?php
}

?>
</p>
