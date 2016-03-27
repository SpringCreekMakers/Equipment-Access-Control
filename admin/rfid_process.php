<?php

/* Credits:
* 	Code based off of example code from: http://www.sitepoint.com/proc-open-communicate-with-the-outside-world/
*/

// descriptor array
$desc = array(
    0 => array('pipe', 'r'), // 0 is STDIN for process
    1 => array('pipe', 'w'), // 1 is STDOUT for process
    2 => array('pipe', 'w') // 2 is STDERR for process
);

// command to invoke markup engine
//$cmd = "access_control --gtag";
$cmd = "./access_control_php_reg";

// working directory of script
$cwd = "/home/pi/access_control/";

// spawn the process
$p = proc_open($cmd, $desc, $pipes, $cwd);

// close the input pipe so the process knows 
// not to expect more input and can start processing
fclose($pipes[0]);

// read the output from the engine
$html = stream_get_contents($pipes[1]);
$error = stream_get_contents($pipes[2]); // note: we can check to see if this is NULL

// all done! Clean up
fclose($pipes[1]);
fclose($pipes[2]);
proc_close($p);

echo $html;
echo $error;


?>