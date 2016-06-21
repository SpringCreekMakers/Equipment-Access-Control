<?php

$first_name = $_REQUEST["first_name"];
$last_name = $_REQUEST["last_name"];
$adder = $_REQUEST["adder"];
$addee = $_REQUEST["addee"];

$db = new mysqli('10.1.10.75', 'testr_pac', 'Testing12345#', 'r_pac');

if($db->connect_errno > 0){
    die('Unable to connect to database [' . $db->connect_error . ']');
};

$sql = $db->prepare("CALL kiosk_add_member(0,0,'authentication',?,?,?,?)");

$sql->bind_param('ssss', $adder, $addee, $first_name, $last_name);

$sql->execute();

echo "success";



$db->close();
?>