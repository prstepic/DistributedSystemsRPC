#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"
#include <ctime>
#include <chrono>
#include "TNSService.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using TNSService::command_info;
using TNSService::server_status;
using TNSService::current_user;
using TNSService::following_user_message;
using TNSService::post_info;
using TNSService::user_services;
using TNSService::client_info;
using TNSService::available_server;
using TNSService::available_status;

// globals that represent the connected server's ip and the router information
std::string connected_server_ip = "";
std::string connected_server_port = "";
std::string router_name = "";
std::unique_ptr<user_services::Stub> router_stub;

// helper function that will contact the router and get a new available server
std::vector<std::string> get_new_server(){
	client_info info_to_send;
	info_to_send.set_ip_server(connected_server_ip);
	info_to_send.set_port(connected_server_port);
	ClientContext context;
	available_server returned_server;
	// send a request to the router for a new available server
	Status status = router_stub->RequestForServer(&context, info_to_send, &returned_server);
	// if the status was not ok, the router could not be reached or produced an error
	if(!status.ok()){
		std::vector<std::string> empty_vector;
		std::cout<<"router not online"<<std::endl;
		return empty_vector;
	}
	else{
		// return the new server's ip and port 
		std::vector<std::string> return_info;
		return_info.push_back(returned_server.ip_addr());
		return_info.push_back(returned_server.port());
		
		return return_info;
	}
	
}

class Client : public IClient
{
	public:
		// constructor to set hostname, username, and port number
		Client(const std::string& hname,
		       const std::string& uname,
		       const std::string& p);
		void connection_check();
		// this function will create a stub for the Client class so
		// it can interface with TNSService
		void create_stub() {
			std::string connection_name = hostname + ":" + port;
					stub_ = std::unique_ptr<user_services::Stub>(
									user_services::NewStub(grpc::CreateChannel(connection_name, 
									grpc::InsecureChannelCredentials())));
	}
	protected:
		virtual int connectTo();
		virtual IReply processCommand(std::string& input);
		virtual void processTimeline();
		IReply follow_user(std::string user_to_follow);
		IReply unfollow_user(std::string user_to_unfollow);
		IReply list_followers();
	private:
		std::string hostname;
		std::string username;
		std::string port;
		int server_switched = 0; // variable will allow all threads to update stub when server changes

		// You can have an instance of the client stub
		// as a member variable.
		std::unique_ptr<user_services::Stub> stub_;
};

int main(int argc, char** argv) {

	std::string hostname = "localhost";
	std::string username = "default";
	std::string port = "3010";
	int opt = 0;
	while ((opt = getopt(argc, argv, "h:u:p:r:")) != -1){
		switch(opt) {
		    case 'h':
			hostname = optarg;break;
		    case 'u':
			username = optarg;break;
		    case 'p':
			port = optarg;break;
		    case 'r':
			router_name = optarg;break;
		    default:
			std::cerr << "Invalid Command Line Argument\n";
		}
	}
	// if no command line argument for the router were provided, ask the user for the router information
	if(router_name == ""){
		std::cout << "Please enter the router ip address and port number in the form <ip>:<port> (ie. ###.###.###.###:####)" << std::endl;
		std::cin >> router_name;
		// create the stub for client to router connection
		router_stub = std::unique_ptr<user_services::Stub>(user_services::NewStub(grpc::CreateChannel(router_name, grpc::InsecureChannelCredentials())));
	}
	// END is used by the client and server, having a username would mess up logic
	if(username == "END"){
		std::cout<<"Invalid username"<<std::endl;
	}
	// get an available server from the router, notify user if router connection failed
	std::vector<std::string> initial_server = get_new_server();
	if(initial_server.size() == 0){
		std::cout<<"Router could not be connected to. Please try again later."<<std::endl;
		std::cout<<"This could be caused by a faulty port. For grading, place router on new port."<<std::endl;
		exit(0);
	}
	Client myc(initial_server.at(0), username, initial_server.at(1));
	// thread that will run the main client logic
	// You MUST invoke "run_client" function to start business logic
	std::thread client_main([&myc](){
		myc.run_client();
	});
	// thread that will check the connection with the connected server
	std::thread server_check([&myc](){
		sleep(.5); // wait for the stub to be created
		while(1){
			myc.connection_check();
		}
	});
	client_main.join();
	server_check.join();
	return 0;
}

// sets the hostname, username, and port
Client::Client(const std::string& hname, const std::string& uname,
		       const std::string& p):hostname(hname), username(uname), port(p){
    	
}

// this function will keep sending requests to the server to see if it's still on
// if it isn't change the stub connection to connect to the router and then connect to the server
void Client::connection_check(){
		
		ClientContext context;
		std::shared_ptr<ClientReaderWriter<available_status, available_status>> stream(
            stub_->Ping(&context));
		available_status on;
		on.set_available(1);
		bool servers_online = 1;
		
		while(1){
			// every half second send an online message to the connected server
			sleep(.5);
			stream->Write(on);
			
			available_status received;
			received.set_available(0);
			stream->Read(&received);
			// if no message from the server was received reconnect to another one
			if(received.available() != 1){
				// get a new available server from the router
				std::vector<std::string> new_server = get_new_server();
				if(new_server.size() != 0 && new_server.at(0) != "ERROR"){
					// tell the user that a reconnection is happening
					displayReConnectionMessage(new_server.at(0), new_server.at(1));
					std::string connection_name = new_server.at(0) + ":" + new_server.at(1);
					// update the client stub;
					stub_ = std::unique_ptr<user_services::Stub>(
									user_services::NewStub(grpc::CreateChannel(connection_name, 
									grpc::InsecureChannelCredentials())));
					// update the current connected server
					connected_server_ip = new_server.at(0);
					connected_server_port = new_server.at(1);
					// initialize the server switched variable
					server_switched = 1;
					current_user username_to_send;
					username_to_send.set_username(username);
					server_status returned_status;
					ClientContext newContext;
					// update the server that a new client has connected
					Status status = stub_->InitializeUser(&newContext, username_to_send, &returned_status);
					break;
				}
				else { // if there are no available servers, tell the user
					std::cout<<"Fatal: no servers on"<<std::endl;
					std::cout<<"Please quit and try again later"<<std::endl;
					break;
				}
			}
		}
}

// this function will request to follow a user and return the server's reply
IReply Client::follow_user(std::string user_to_follow){
	// set the info to send to the server
	command_info info_to_send;
	info_to_send.set_username(this->username);
	info_to_send.set_username_other_user(user_to_follow);

	// variable to get data from server
	server_status returned_status;

	// Use the stub to send a follow request to the server
	// get the IStatus of the return struct
	ClientContext context;
	Status status = stub_->FollowRequest(&context, info_to_send, &returned_status);
	IStatus status_to_return;
	status_to_return = (IStatus)returned_status.s_status();
	
	// make sure the grpc connection is still valid and return the IReply object
	IReply ire;
	if (!status.ok()) {
		std::cout << status.error_code() << ": " << status.error_message()
			<< std::endl;
		std::cout << "RPC failed";
    	}
	ire.grpc_status = status;
	ire.comm_status = status_to_return;
	return ire;
}

// this function will request to unfollow a user and will return the server's reply
IReply Client::unfollow_user(std::string user_to_unfollow){

	// set up the command to send to the user
	command_info info_to_send;
	info_to_send.set_username(this->username);
	info_to_send.set_username_other_user(user_to_unfollow);

	// variable to store returned data from the user
	server_status returned_status;
	ClientContext context;
	
	// Use the stub to make an unfollow request to the server
	Status status = stub_->UnfollowRequest(&context, info_to_send, &returned_status);
	IStatus status_to_return;
	status_to_return = (IStatus)returned_status.s_status();

	// Make sure the rpc connection is still valid and return the IReply object
	if (!status.ok()) {
      		std::cout << status.error_code() << ": " << status.error_message()
                	<< std::endl;
      		std::cout << "RPC failed";
    	}
	IReply ire;
	ire.grpc_status = status;
	ire.comm_status = status_to_return;
	return ire;
}

// this function will request to see all the users on the server and all of this user's followers
IReply Client::list_followers(){
	
	// set up message to send to server with user's name
	current_user this_user;
	ClientContext context;
	
	// the server will return a stream of followers and all users in the server
	// these lists will be stored in vectors
	following_user_message follower;
	std::vector<std::string> list_of_followers;
	std::vector<std::string> list_all_users;

	// set the message's username
	this_user.set_username(this->username);
	IStatus status_to_return;

	// Stub will make the list request to the server
	std::unique_ptr<ClientReader<following_user_message>> reader (stub_->ListRequest(&context, this_user));

	// read in from the stream until the server stops sending messages
	// END represents that the end of the all users list or follower list has been reached
	while(reader->Read(&follower)){
		if(follower.username() != "END"){
			list_of_followers.push_back(follower.username());
		}
		if(follower.user_in_all_users() != "END"){
			list_all_users.push_back(follower.user_in_all_users());
		}
		status_to_return = (IStatus)follower.s_status();
	}
	
	// return the reply structure built from data from the server
	IReply ire;
	Status status = reader->Finish();
	ire.grpc_status = status;
	ire.comm_status = status_to_return;
	ire.all_users = list_all_users;
	ire.followers = list_of_followers;
	return ire;
}

// function that will establish connection to the server
int Client::connectTo()
{
	// ------------------------------------------------------------
	// In this function, you are supposed to create a stub so that
	// you call service methods in the processCommand/porcessTimeline
	// functions. That is, the stub should be accessible when you want
	// to call any service methods in those functions.
	// I recommend you to have the stub as
	// a member variable in your own Client class.
	// Please refer to gRpc tutorial how to create a stub.
	// ------------------------------------------------------------
	//std

	// create the stub that this class can use to make requests
	create_stub();
	connected_server_ip = hostname;
	connected_server_port = port;
	// send an initialization message to the server
	// so the server can register the user into its database
	current_user username_to_send;
	username_to_send.set_username(username);
	server_status returned_status;
	ClientContext context;
	
	Status status = stub_->InitializeUser(&context, username_to_send, &returned_status);
	
	// if the username already exists
	if((IStatus)returned_status.s_status() == FAILURE_ALREADY_EXISTS){
		std::cout<<"Username already taken"<<std::endl;
		return -1;
	} 

	// Make sure that the grpc connection is still valid
	if (!status.ok()) {
		std::cout << status.error_code() << ": " << status.error_message()
        		<< std::endl;
		std::cout << "RPC failed";
		return -1;
	}
	return 1; // return 1 if success, otherwise return -1
}

// function that will process commands from the user from the command line
IReply Client::processCommand(std::string& input)
{
	// ------------------------------------------------------------
	// GUIDE 1:
	// In this function, you are supposed to parse the given input
	// command and create your own message so that you call an 
	// appropriate service method. The input command will be one
	// of the followings:
	//
	// FOLLOW <username>
	// UNFOLLOW <username>
	// LIST
	// TIMELINE
	//
	// - JOIN/LEAVE and "<username>" are separated by one space.
	// ------------------------------------------------------------
	IReply ire;

	// check the command of the input
	if(input.substr(0,6) == "FOLLOW"){

		// request a follow request to the server with the username argument
		ire = follow_user(input.substr(7));
		return ire;
	}
	else if(input.substr(0,8) == "UNFOLLOW"){
		
		// request to unfollow the user in the argument
		ire = unfollow_user(input.substr(9));
		return ire;
	}
	else if(input.substr(0,4) == "LIST"){

		// make a request to the server to list followers and all users
		ire = list_followers();
		return ire;
	}
	else if(input.substr(0, 8) == "TIMELINE"){ // timeline mode command

		// Create IReply that will be used to enter timeline mode
		ire.grpc_status = Status::OK;
		ire.comm_status = SUCCESS;
		return ire;
	}
	else{ // default to an invalid command
		std::cout<<"Invalid command"<<std::endl;
	}
	
	// ------------------------------------------------------------
	// GUIDE 2:
	// Then, you should create a variable of IReply structure
	// provided by the client.h and initialize it according to
	// the result. Finally you can finish this function by returning
	// the IReply.
	// ------------------------------------------------------------

	// ------------------------------------------------------------
	// HINT: How to set the IReply?
	// Suppose you have "Join" service method for JOIN command,
	// IReply can be set as follow:
	// 
	//     // some codes for creating/initializing parameters for
	//     // service method
	//     IReply ire;
	//     grpc::Status status = stub_->Join(&context, /* some parameters */);
	//     ire.grpc_status = status;
	//     if (status.ok()) {
	//         ire.comm_status = SUCCESS;
	//     } else {
	//         ire.comm_status = FAILURE_NOT_EXISTS;
	//     }
	//      
	//      return ire;
	// 
	// IMPORTANT: 
	// For the command "LIST", you should set both "all_users" and 
	// "followers" member variable of IReply.
	// ------------------------------------------------------------

}

// function to handle functionality when the user enters timeline mode
void Client::processTimeline()
{
	// ------------------------------------------------------------
	// In this function, you are supposed to get into timeline mode.
	// You may need to call a service method to communicate with
	// the server. Use getPostMessage/displayPostMessage functions
	// for both getting and displaying messages in timeline mode.
	// You should use them as you did in hw1.
	// ------------------------------------------------------------
	// ------------------------------------------------------------
	// IMPORTANT NOTICE:
	//
	// Once a user enter to timeline mode , there is no way
	// to command mode. You don't have to worry about this situation,
	// and you can terminate the client program by pressing
	// CTRL-C (SIGINT)
	// -----------------------------------------------------------
	

	// Use stub to create a stream that the user and server can communicate through
	// They will commnicate through Timeline requests
	

	// thread that will send post info with
	std::string current_user = this->username; 
	std::thread writer([this]() {
		std::string msg;
		// infinite loop, client won't be able to exit timeline mode
		while(1) {
			ClientContext context;
			std::shared_ptr<ClientReaderWriter<post_info, post_info>> stream(
            stub_->TimelineRequest(&context));
			while (1) {
				// if the server has been switched increment the server_switched variable and sleep for 2 seconds
				// once server_switched reaches 4, then all threads have been update of the switch
				if(server_switched > 0){
					if(server_switched < 4){
						server_switched++;
					}
					else{
						server_switched = 0;
					}
					sleep(2);
					break;
				}
				// get message from the user from the command line
				msg = getPostMessage();
			
				// set the contents of the message username, time, post
				post_info info_to_send;
				info_to_send.set_username(this->username);

				// get the current time of the post
				auto current_time = std::chrono::system_clock::now();
				std::time_t time_of_post = std::chrono::system_clock::to_time_t(current_time);
				displayPostMessage(this->username, msg, time_of_post);
				// convert time to a string and send the message to the server
				info_to_send.set_time(std::ctime(&time_of_post));
				info_to_send.set_content(msg);
				info_to_send.set_requesting_update(0);
				stream->Write(info_to_send);
			
			}
			
		}
		
    	});
	
	// this thread will continually send update requests to the server 
	// this will get any new posts from followers
	std::thread update([this]() {
		
		post_info update_info;
		update_info.set_username(this->username);
		update_info.set_requesting_update(1);
		while(1){
			ClientContext context;
			std::shared_ptr<ClientReaderWriter<post_info, post_info>> stream(
            stub_->TimelineRequest(&context));
			while(1){
				// if the server has been switched increment the server_switched variable and sleep for 2 seconds
				// once server_switched reaches 4, then all threads have been update of the switch
				if(server_switched > 0){
					if(server_switched < 4){
						server_switched++;
					}
					else{
						server_switched = 0;
					}
					sleep(2);
					break;
				}
				stream->Write(update_info);
				// only make a request every 1 sec
				sleep(1);
			}
		}
	});

	// thread that will read from the stream whenever data becomes available
	std::thread reader([this]() {
		post_info info_to_read;
		// when ther is data in the stream
		while(1){
			ClientContext context;
			std::shared_ptr<ClientReaderWriter<post_info, post_info>> stream(
		    stub_->TimelineRequest(&context));
			while(stream->Read(&info_to_read)){
				std::string post_user = info_to_read.username();
				// make sure the post isn't a message indicating the end of a timeline
				if(post_user != "END"){
					// convert the time string to a time_t and display the message to the user
					std::string post_time = info_to_read.time();
					std::string post_content = info_to_read.content();
					//stackoverflow.com/questions/11213326/
					struct tm tm;
					strptime(post_time.c_str(), "%a %b %d %T %Y", &tm);	
					time_t post_time_time_t = mktime(&tm);
					displayPostMessage(post_user, post_content, post_time_time_t);
				}
				// if the server has been switched increment the server_switched variable and sleep for 2 seconds
				// once server_switched reaches 4, then all threads have been update of the switch
				if(server_switched > 0){
					if(server_switched < 4){
						server_switched++;
					}
					else{
						server_switched = 0;
					}
					sleep(2);
					break;
				}
			}
		}
		
	});

	// join all threads when they are done executing
	update.join();
	writer.join();
	reader.join();





}
