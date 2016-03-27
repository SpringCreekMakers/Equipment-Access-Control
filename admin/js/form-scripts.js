// none, bounce, rotateplane, stretch, orbit,
    // roundBounce, win8, win8_linear or ios
    var current_effect = 'bounce'; //


("#newMemberForm").submit(function(event){
    // cancels the form submission
    event.preventDefault();
    submitForm();
});

function submitForm(){
    // Initiate Variables With Form Content
    var name = $("#name").val();
    var email = $("#email").val();
    var message = $("#message").val();
 
    $.ajax({
        type: "POST",
        url: "php/form-process.php",
        data: "name=" + name + "&email=" + email + "&message=" + message,
        success : function(text){
            if (text == "success"){
                formSuccess();
            }
        }
    });
}
function formSuccess(){
    $( "#msgSubmit" ).removeClass( "hidden" );
}

function getRFIDCurrentMember(){
    run_waitMe(current_effect);
    $.ajax({
        type: "GET",
        url: "rfid_process.php",
        success : function(text){
           $('.containerBlock').waitMe('hide');
            $("#adder").val(text);
        }
    });
}

function getRFIDNewMember(){
    run_waitMe(current_effect);
    $.ajax({
        type: "GET",
        url: "rfid_process.php",
        success : function(text){
            $('.containerBlock').waitMe('hide');
            $("#addee").val(text);
        }
    });
}

function run_waitMe(effect){
        $(".containerBlock").waitMe({
            effect: effect,
            text: 'Please Scan Badge/Key Fob...',
            bg: 'rgba(255,255,255,0.7)',
            color: '#000',
            maxSize: '',
            source: 'img.svg',
            onClose: function() {}
        });
    }
