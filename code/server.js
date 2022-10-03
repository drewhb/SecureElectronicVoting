const express = require('express');

// Node.js Web App
var app = express();
var server = require('http').createServer(app);
var udp = require('dgram').createSocket('udp4');
var fs = require('fs'); 
var esp32;

// Data Var
let vote_flag = -1; // -1 is off, 0 is on
let r = 0;
let g = 0;
let y = 0;


app.set('view engine', 'ejs');
app.get('/', (req, res) => {
	// console.log("Connected: " + req.baseUrl);

	res.render("index", {
		vote_status: vote_flag,
		r_stat: r,
		g_stat: g,
		y_stat: y
	});
});

// Post Requests
app.post('/', (req, res) => {
	console.log("Sending back Vote signal");
	console.log("OLD Flag: " + vote_flag);
    vote_flag = ~vote_flag;
    r = 0;
    y = 0;
    g = 0;
	console.log("NEW Flag: " + vote_flag);
});

udp.on('listening', function () {
  var address = udp.address();
  console.log('UDP Server listening on ' + address.address + ":" + address.port);
});
var line = []; 
// On connection, print out received message
udp.on('message', function (message, remote) {
	console.log(remote.address + ':' + remote.port +'   ' + message);
    
	const data = message.toString();

    if(data == "R"){
        r++;
    }
    else if(data == "G"){
        g++;
    }
    else if (data == "Y") {
        y++;
    }
});

udp.bind(3000);
server.listen(3000), () => {
	console.log("Hosting node.js on: longchicken.hopto.org");
};