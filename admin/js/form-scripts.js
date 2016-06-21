// none, bounce, rotateplane, stretch, orbit,
    // roundBounce, win8, win8_linear or ios
    var current_effect = 'bounce'; //


/* ("#newMemberForm").submit(function(event){
    // cancels the form submission
    event.preventDefault();
    submitForm();
});
*/ 
function submitForm(){
    // Initiate Variables With Form Content
    var first_name_var = $("#first_name").val();
    var last_name_var = $("#last_name").val();
    var adder_var = $("#adder").val();
    var addee_var = $("#addee").val();
    
 
    $.ajax({
        type: "POST",
        url: "form_process.php",
        data: {first_name: first_name_var, last_name: last_name_var, adder: adder_var, addee: addee_var},
        success : function(text){
            if (text == "success"){
                formSuccess();
            }
        }
    });
}
function formSuccess(){
    $('#newMemberForm').trigger('reset');
    $( "#mbrSubmit" ).removeClass( "hidden" );
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
