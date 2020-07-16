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

class Client : public IClient
{
	public:
		Client(const std::string& hname,
		       const std::string& uname,
		       const std::string& p);
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

		// You can have an instance of the client stub
		// as a member variable.
		std::unique_ptr<user_services::Stub> stub_;
};

int main(int argc, char** argv) {

	std::string hostname = "localhost";
	std::string username = "default";
	std::string port = "3010";
	int opt = 0;
	while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
		switch(opt) {
		    case 'h':
			hostname = optarg;break;
		    case 'u':
			username = optarg;break;
		    case 'p':
			port = optarg;break;
		    default:
			std::cerr << "Invalid Command Line Argument\n";
		}
	}

	if(username == "END"){
		std::cout<<"Invalid username"<<std::endl;
	}
	
	std::cout<<"Initializing client"<<std::endl;
	Client myc(hostname, username, port);
	// You MUST invoke "run_client" function to start business logic
	std::cout<<"Running client"<<std::endl;
	myc.run_client();

	return 0;
}

Client::Client(const std::string& hname, const std::string& uname,
		       const std::string& p):hostname(hname), username(uname), port(p){
	/*current_user username_to_send;
	username_to_send.set_username(hname);
	server_status returned_status;
	ClientContext context;
	
	Status status = stub_->InitializeUser(&context, username_to_send, &returned_status);
	std::cout<<"Stub created"<<std::endl;
	// if the username already exists
	if((IStatus)returned_status.s_status() == FAILURE_ALREADY_EXISTS){
		std::cout<<"Username already taken"<<std::endl;
	} 

	if (!status.ok()) {
		std::cout << status.error_code() << ": " << status.error_message()
        		<< std::endl;
		std::cout << "RPC failed";
	}*/
    	
}

IReply Client::follow_user(std::string user_to_follow){
	command_info info_to_send;
	info_to_send.set_username(this->username);
	info_to_send.set_username_other_user(user_to_follow);
	server_status returned_status;
	ClientContext context;
	Status status = stub_->FollowRequest(&context, info_to_send, &returned_status);
	IStatus status_to_return;
	status_to_return = (IStatus)returned_status.s_status();
	
	if(status_to_return == FAILURE_INVALID){
		std::cout<<"Error: you can't follow yourself"<<std::endl;
	}
	
	if(status_to_return == FAILURE_NOT_EXISTS){
		std::cout<<"Error: user not found"<<std::endl;
	}
	
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

IReply Client::unfollow_user(std::string user_to_unfollow){
	command_info info_to_send;
	info_to_send.set_username(this->username);
	info_to_send.set_username_other_user(user_to_unfollow);
	server_status returned_status;
	ClientContext context;
	Status status = stub_->UnfollowRequest(&context, info_to_send, &returned_status);
	IStatus status_to_return;
	status_to_return = (IStatus)returned_status.s_status();

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

IReply Client::list_followers(){
	current_user this_user;
	ClientContext context;
	following_user_message follower;
	std::vector<std::string> list_of_followers;
	std::vector<std::string> list_all_users;
	this_user.set_username(this->username);
	IStatus status_to_return;
	std::unique_ptr<ClientReader<following_user_message>> reader (stub_->ListRequest(&context, this_user));
	while(reader->Read(&follower)){
		if(follower.username() != "END"){
			list_of_followers.push_back(follower.username());
		}
		if(follower.user_in_all_users() != "END"){
			list_all_users.push_back(follower.user_in_all_users());
		}
		status_to_return = (IStatus)follower.s_status();
	}
	IReply ire;
	Status status = reader->Finish();
	ire.grpc_status = status;
	ire.comm_status = status_to_return;
	ire.all_users = list_all_users;
	ire.followers = list_of_followers;
	return ire;
}

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

	create_stub();
	current_user username_to_send;
	username_to_send.set_username(username);
	server_status returned_status;
	ClientContext context;
	
	Status status = stub_->InitializeUser(&context, username_to_send, &returned_status);
	std::cout<<"Stub created"<<std::endl;
	// if the username already exists
	if((IStatus)returned_status.s_status() == FAILURE_ALREADY_EXISTS){
		std::cout<<"Username already taken"<<std::endl;
	} 

	if (!status.ok()) {
		std::cout << status.error_code() << ": " << status.error_message()
        		<< std::endl;
		std::cout << "RPC failed";
	}
	return 1; // return 1 if success, otherwise return -1
}

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
	if(input.substr(0,6) == "FOLLOW"){
		std::cout<<"Follow request"<<std::endl;
		ire = follow_user(input.substr(7));
		return ire;
		
	}
	else if(input.substr(0,8) == "UNFOLLOW"){
		ire = unfollow_user(input.substr(9));
		return ire;
	}
	else if(input.substr(0,4) == "LIST"){
		ire = list_followers();
		return ire;
	}
	else{
		ire.grpc_status = Status::OK;
		ire.comm_status = SUCCESS;
		return ire;
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
	ClientContext context;

	std::shared_ptr<ClientReaderWriter<post_info, post_info>> stream(
            stub_->TimelineRequest(&context));

	// thread that will send post info with
	std::string current_user = this->username; 
	std::thread writer([this, stream]() {
		std::string msg;
		while (1) {
			msg = getPostMessage();
			std::cout<<msg<<std::endl;
			post_info info_to_send;
			info_to_send.set_username(this->username);
			auto current_time = std::chrono::system_clock::now();
			std::time_t time_of_post = std::chrono::system_clock::to_time_t(current_time);
			info_to_send.set_time(std::ctime(&time_of_post));
			info_to_send.set_content(msg);
			info_to_send.set_requesting_update(0);
			stream->Write(info_to_send);
			
		}
		stream->WritesDone();
    	});
	
	std::thread update([this, stream]() {
		
		post_info update_info;
		update_info.set_username(this->username);
		update_info.set_requesting_update(1);
		while(1){
			stream->Write(update_info);
			usleep(1000000);
		}
	});

	std::thread reader([this, stream]() {
		
		post_info info_to_read;
		while(stream->Read(&info_to_read)){
			std::string post_user = info_to_read.username();
			if(post_user != "END"){
				std::string post_time = info_to_read.time();
				std::string post_content = info_to_read.content();
				//stackoverflow.com/questions/11213326/
				struct tm tm;
				strptime(post_time.c_str(), "%a %b %d %T %Y", &tm);	
				time_t post_time_time_t = mktime(&tm);
				displayPostMessage(post_user, post_content, post_time_time_t);
			}
		}
			
		
	});

	update.join();
	writer.join();
	reader.join();





}
