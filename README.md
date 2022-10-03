# Electronic Voting
Authors: Andrew Brownback, Mark Vinciguerra, Timin Kanani

Date: 2022-04-05
-----

## Summary

In this skill we implemented a secure voting system that allows votes to be secretly passed between fobs via IR, so that doesn't have to go through a network to cast its vote. Though a network will eventually be used when sending voting data to the database, the security risks can be otherwise mitigated through means described below. 

## Investigative Question

### Security Issues
1.  Denial of Service: In this attack, the bad guy would launch bots, all using an extreme amount of internet bandwidth that would overwhelm the traffic on the BU wifi network that would not allow our leader to count and cast the votes to the server. 
2.  Freeloader: Similar to DoS, the attacker could connect to our local internet router and take up bandwidth causing the same issues as DoS would cause. 
3.  Eavesdroppers: By getting close our router and connecting to it either physically or wirelessly, the attacker could intercept our voter data and steal it or manipulate it. By doing this they could potentially steal the data, or manipulate it before it reaches the data base. They could do this quite easily by simply knowing the IP address of our leader, since this is the esp which sends the votes to the server.  
4.  Endpoint Attack: They could physically replace the esp-32 on our voter fob without us realizing and could do any number of malicious things. For example the atacker could prevent a vote from happening, create fraudulent voting data tricking us into believing that this data is real, or steal the data by sending it to an alternative source even potentially allowing it to become public. 
5.  Spoilers: They could send bad requests to our server and break up the signal, which would create issues with our UDP packets to our server. If these packets are not recieved by the server, they could therefore prevent a vote from happening. 

### Prevention
1.  In order to mitigate a DoS attack, we could potentially keep track of network bandwidth and if a spike is detected, we could have a backup internet source that our program would automatically force our ESPs to go offline and connect to temporarily like a mobile hotspot. 

2.  For this type of attack the most clear way to mitigate a freeloader is to enhance our netowrk security by creating a more sophisticated password that is many alphanumeric characters, we could hide our SSID requiring users to know it in order to connect, and lastly we could even implement two factor authentication. 

3.  To prevent this type of attack we could relocate our voting station to a more secure location, and have it connect to a more secure network so it isn't dependent on the large and public BU network.

4.  We could implement a system that detects if our esps become dissconnected from our fob, granted which would only work if they were removed while online. If they were offline we could try and make it so that no unknown esp IP addresses could connect to our network. 

5.  We could try and detect the source of all requests to our server, and block any that don't match a predetermined criteria. 

## Solution Design

In this skill we created an end-to-end to E-voting system that uses fobs equipped with IR transmitters and receivers. Each fob secretly tells its vote to another fob through IR, and then that vote is sent to the Poll Leader (another ESP) through UDP. The Poll Leader receives each vote and forwards it to our server via UDP, which is run on the raspberry pi. The server displays the information about each candidate (in this case, R, G, or Y which corresponds to the red, green, or blue LED on the fob), and counts the votes. 


## Sketches and Photos
<center><img src="./images/ece444.png" width="25%" /></center>  
<center> </center>


## Supporting Artifacts
- [Link to video demo! ==>](https://www.youtube.com/watch?v=7NL48_KedVY).

- [Link to our Code! ==>](https://github.com/BU-EC444/Team11-Team-Hobbit-Brownback-Vinciguerra-Kanani/tree/master/quest-4/code).

## Modules, Tools, Source Used Including Attribution

We used the below links to help in this quest, including getting the server running on the raspberry pi, and while we didn't get the database going we tryed to use it to help us. 

## References

https://conoroneill.net/2015/09/20/tingodb-and-sqlite-instead-of-mongodb-and-mysql-for-tiny-projects-or-raspberry-pi/

https://github.com/BU-EC444/esp-idf/tree/master/examples/protocols/sockets/udp_server

https://github.com/BU-EC444/esp-idf/tree/master/examples/protocols/sockets/udp_client

https://www.mongodb.com/developer/how-to/mongodb-on-raspberry-pi/


-----

