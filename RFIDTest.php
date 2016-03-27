<?php

$cmd = "./access_control"; //example command
$cwd = "/home/pi/access_control/";

$descriptorspec = array(
    0 => array("pipe", "r"), 
    1 => array("pipe", "w"), 
    2 => array("pipe", "a")
);

$pipes = array();

$process = proc_open($cmd, $descriptorspec, $pipes, $cwd, null);

echo "Start process:\n";

$str = "";

if(is_resource($process)) {
    do {
        $curStr = fgets($pipes[1]);  //will wait for a end of line
        echo $curStr;
        $str .= $curStr;

        $arr = proc_get_status($process);

    }while($arr['running']);
}else{
    echo "Unable to start process\n";
}

fclose($pipes[0]);
fclose($pipes[1]);
fclose($pipes[2]);
proc_close($process);

echo "\n\n\nDone\n";

echo "Result is:\n----\n".$str."\n----\n";

?>