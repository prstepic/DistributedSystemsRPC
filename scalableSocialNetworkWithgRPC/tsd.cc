#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <queue>
#include <thread>
#include <unistd.h>
#include <fstream>
#include <signal.h>
#include <sys/prctl.h>
#include <grpc++/grpc++.h>

#include "TNSService.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using TNSService::user_services;
using TNSService::command_info;
using TNSService::server_status;
using TNSService::current_user;
using TNSService::following_user_message;
using TNSService::post_info;
using TNSService::available_server;
using TNSService::available_status;

// user struct that contains essential information for each user
struct user {
	
	std::string username = "";
	std::vector<std::string> followers;
	std::vector<std::string> following;
	std::queue<std::vector<std::string>> timeline;
	std::vector<std::vector<std::string>> posts;
};

// globals for this process' ip and port and the router machine
std::string port = "3010";
std::string ipAddr = "localhost";
std::string router = "localhost:3000";

// helper function that will create a new slave process when one is killed
int new_slave(){
	// create a new slave process
	if(fork() == 0){
		signal(SIGINT, SIG_IGN);
		std::string connection_name= ipAddr + ":" + port;
		
		std::unique_ptr<user_services::Stub> slave_stub(user_services::NewStub(grpc::CreateChannel(connection_name, grpc::InsecureChannelCredentials())));
		
		ClientContext context;
		// re-establish the ping rpc with the master process
		std::shared_ptr<ClientReaderWriter<available_status, available_status>> stream(
            slave_stub->Ping(&context));
		// set slave's status to on
		available_status on;
		on.set_available(1);
		
		while(1){
			// write to the master process every two seconds
			stream->Write(on);
			sleep(2);
			available_status received;
			received.set_available(0);
			stream->Read(&received);
			// if nothing was read from the master process restart the server completely
			// exec will place a new tsd process in place of the slave process 
			if(received.available() != 1){
				kill(getppid(), SIGKILL);
				sleep(2);
				execlp("./tsd","./tsd", "-i", ipAddr.c_str(), "-p", port.c_str(),"-r", router.c_str(), (char*) NULL);
			}
		}
	}
	else{ // parent process
		return 1;
	}
}
// function that will find the index of a username within a vector
int find_follower(std::vector<std::string> v, std::string u){
	for(int i = 0; i < v.size(); i++){
		if(v.at(i) == u){
			return i;
		}
	}
	return -1;
}

// Hash map that will be used to store all user objects
std::unordered_map<std::string, user*> users_db;

// vector to contain the usernames of all users that have ever connected to the server
std::vector<std::string> all_users;

// file streams that will be used to read and write to the server log
std::ifstream old_log_file;
std::ofstream new_log_file;

// server implementation of TNSService
class TNSServiceImpl final : public user_services::Service{
	
	// this function will log new users that connect into the database and all users list
	Status InitializeUser(ServerContext* context, const current_user* request, server_status* response) override {
		
		// make sure the username doesn't already exist
		std::string requesting_user = request->username();
		
		if(users_db.find(requesting_user) != users_db.end()){
			
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_ALREADY_EXISTS);
		}
		else{
			// create a new user object set it's name and enter into the database and all users
			user* user_to_insert = new user();
			user_to_insert->username = requesting_user;
			users_db.insert(std::pair<std::string, user*>(requesting_user, user_to_insert));
			all_users.push_back(requesting_user);

			// by default the user will follow themselves
			users_db.at(requesting_user)->followers.push_back(requesting_user);
			users_db.at(requesting_user)->following.push_back(requesting_user);
			
			// write an initialize command to the log file
			new_log_file << "INITIALIZE " + requesting_user +"\n";
			
		}
		
		// set the response as successful and return an OK grpc status
		response->set_s_status(TNSService::server_status_IStatus_SUCCESS);
		
		return Status::OK;
	}

	// This function will handle when a user requests to follow another user
	Status FollowRequest(ServerContext* context, const command_info* request, server_status* response) override {
		
		// get the user requesting a follow and the user that wants to be followed
		std::string requesting_user = request->username();
		std::string user_to_follow = request->username_other_user();
		
		// make sure the requested user exists
		if(users_db.find(user_to_follow) == users_db.end()){
			
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_NOT_EXISTS);
		}

		// make sure the user isn't requesting to follow themselves
		else if(user_to_follow == requesting_user){
			
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_INVALID);
		}
		
		else{
			// add the requested user to follow to the requesting user's following list
			// and add the requesting user to the requested user's followers list
			users_db.at(requesting_user)->following.push_back(user_to_follow);
			users_db.at(user_to_follow)->followers.push_back(requesting_user);
			response->set_s_status(TNSService::server_status_IStatus_SUCCESS);
			
			// update the user's timeline when they follow
			if(!users_db.at(user_to_follow)->posts.empty()){
				int i = 0;
				int num_posts = users_db.at(user_to_follow)->posts.size();
				while(i != num_posts){
					// the max size of a timeline is 20 posts
					if(users_db.at(requesting_user)->timeline.size() == 20){
						users_db.at(requesting_user)->timeline.pop();
					}
					// add posts to the timeline starting with the earliests first
					// this means when the timeline is popped the latest post is removed
					users_db.at(requesting_user)->timeline.push(
							users_db.at(user_to_follow)->posts.at(i));
					i++;				
				}
			}
			// write the follow request to the log file
			new_log_file << ("FOLLOW " + requesting_user + "|" + user_to_follow + "\n");
		}

		// return an OK grpc status
		return Status::OK;
	}
	
	// this function will handle when a user requests to unfollow anothe user
	Status UnfollowRequest(ServerContext* context, const command_info* request, server_status* response) override {
		
		// get the user requesting a follow and the user that wants to be followed
		std::string requesting_user = request->username();
		std::string user_to_unfollow = request->username_other_user();

		// make sure the requested user exists
		if(users_db.find(user_to_unfollow) == users_db.end()){
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_NOT_EXISTS);
		}

		// make sure the user isn't requesting to unfollow themselves
		else if(user_to_unfollow == requesting_user){
			
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_INVALID);
		}
		
		else{
			// remove the requested user from the requesting user's following list
			// and remove the requesting user to the requested user's followers list
			int position_to_remove1 = find_follower(users_db.at(requesting_user)->following, user_to_unfollow);
			int position_to_remove2 = find_follower(users_db.at(user_to_unfollow)->followers, requesting_user);
			// make sure the user is actually in the followers list
			if(position_to_remove1 != -1 && position_to_remove2 != -1){
				users_db.at(requesting_user)->following.erase(users_db.at(requesting_user)->
									following.begin() + position_to_remove1);
			
				users_db.at(user_to_unfollow)->followers.erase(users_db.at(user_to_unfollow)->
									followers.begin() + position_to_remove2);
			
				response->set_s_status(TNSService::server_status_IStatus_SUCCESS);
				new_log_file << ("UNFOLLOW " + requesting_user + "|" + user_to_unfollow + "\n");
			}
			else{
				response->set_s_status(TNSService::server_status_IStatus_FAILURE_INVALID);
			}
		}
		return Status::OK;
	}

	// this function will handle when a user requests a list
	// the function will send a stream of messages that include users in all users and the user's followers
	Status ListRequest(ServerContext* context, const current_user* request, ServerWriter<following_user_message>* writer) override {
		std::string user_making_request = request->username();

		// keep sending usernames from followers and all users
		// if the end of either list is sent, send "END" as the value in the message
		// all users should always be longer or equal than the followers of the user
		// make check, if it fails send invalid status 
		std::vector<std::string> user_followers = users_db.at(user_making_request)->followers;
		if(!user_followers.empty()){

			// all users should never be less than a user's followers list
			if(all_users.size() < user_followers.size()){
				
				following_user_message return_info;
				return_info.set_username("");
				return_info.set_user_in_all_users("");
				return_info.set_s_status(TNSService::following_user_message_IStatus_FAILURE_INVALID);
				writer->Write(return_info);
			}
			else{
				// for each user that has connected to the database
				for(int i = 0; i < all_users.size(); i++){

					// when the end of the followers list is reached send a END username to the user
					// still will return users in all users
					if(i > user_followers.size() - 1){
						following_user_message return_info;
						return_info.set_username("END");
						return_info.set_user_in_all_users(all_users.at(i));
						return_info.set_s_status(TNSService::following_user_message_IStatus_SUCCESS);
						writer->Write(return_info);
					}

					// if all users and followers are the same length then send END messages
					// for each username at the same time
					else if(i == user_followers.size() -1 && i == all_users.size() -1){
						following_user_message return_info;
						return_info.set_username(user_followers.at(i));
						return_info.set_user_in_all_users(all_users.at(i));
						return_info.set_s_status(TNSService::following_user_message_IStatus_SUCCESS);
						writer->Write(return_info);
		
						return_info.set_username("END");
						return_info.set_user_in_all_users("END");
						return_info.set_s_status(TNSService::following_user_message_IStatus_SUCCESS);
						writer->Write(return_info);
					}

					// send the username of a user's follower and a user in all users
					else{
						following_user_message return_info;
						return_info.set_username(user_followers.at(i));
						return_info.set_user_in_all_users(all_users.at(i));
						return_info.set_s_status(TNSService::following_user_message_IStatus_FAILURE_INVALID);
						writer->Write(return_info);
					}
				}
			}
			return Status::OK;
		}

		// make sure each user in all users is sent to the user if the user doesn't have any followers
		for(int i = 0; i < all_users.size(); i++){
			following_user_message return_info;
			return_info.set_username("END");
			return_info.set_user_in_all_users(all_users.at(i));
			return_info.set_s_status(TNSService::following_user_message_IStatus_SUCCESS);
			writer->Write(return_info);
		}
		
		
		return Status::OK;
	}

	// this function will handle the user's requests when they enter timeline mode
	Status TimelineRequest(ServerContext* context, ServerReaderWriter<post_info, post_info>* stream) override {
		// read in from the stream for messages from users
		// display the sent message to all sending user's followers
		// this must be thread safe - multiple users may send requests at the same time

		// read from the client's stream
		post_info received_info;
		while(stream->Read(&received_info)) {
			bool update_or_post = received_info.requesting_update();
			// user is requesting to post to their timeline
			if(!update_or_post){
				// add post to each followers timeline
				// build a post with username, time, and content
				// store in a vector
				std::string requesting_user = received_info.username();
				std::vector<std::string> user_followers = users_db.at(requesting_user)->followers;
				std::string post_time = received_info.time();
				std::string post_content = received_info.content();
				std::vector<std::string> post_info;
				post_info.push_back(requesting_user);
				post_info.push_back(post_time);
				post_info.push_back(post_content);

				// add the post to the user's posts list
				users_db.at(requesting_user)->posts.push_back(post_info);
				if(!user_followers.empty()){
					// add the post to every followers timeline
					for(int i = 0; i < user_followers.size(); i++){
						// don't add the users post to their timeline, stored in user::posts
						if(users_db.at(user_followers.at(i))->username != requesting_user){
							// remove the oldest post from the timeline if the timeline is longer than 20
							if(users_db.at(user_followers.at(i))->timeline.size() == 20){
								users_db.at(user_followers.at(i))->timeline.pop();
							}
							//add post to the user's timeline
							users_db.at(user_followers.at(i))->timeline.push(post_info);
						}
					}
				}
				
				//removing new lines
				post_time.pop_back();
				post_content.pop_back();
				new_log_file << ("POST " + requesting_user + "|" + post_time + "|" + post_content + "\n");
				
			}
			// user is requesting an update to their timeline
			else{
				// loop until the user no longer has any outstanding posts in their timeline
				while(!users_db.at(received_info.username())->timeline.empty()){
					// build a post from the vector in the user's timeline
					std::vector<std::string> timeline_info = users_db.at(received_info.username())
												->timeline.front();
					users_db.at(received_info.username())->timeline.pop();
					post_info updated_post;
					
					// build the post object
					updated_post.set_username(timeline_info.at(0));
					updated_post.set_time(timeline_info.at(1));
					updated_post.set_content(timeline_info.at(2));
				
					// user doesn't need to be returned their own messages
					if(updated_post.username() != received_info.username()){
						stream->Write(updated_post);
					}
					
				}
				// tell the user the server is done sending posts
				post_info end_post;
				end_post.set_username("END");
				stream->Write(end_post);
			}
		}
		
		return Status::OK;
	}

	// function that will restore the server from the most previous server log
	// will return a list of users that have been initailized in the past
	std::vector<std::string> restore_server(){
		// open the log file
		old_log_file.open("new_server_log.txt");
		std::string history;

		// initialized users must remain persistent through server shutdowns
		std::vector<std::string> initialized_users;
		if(old_log_file.is_open()){
			
			// execute restoration
			while(getline(old_log_file, history)){
				// parse the first word of the line for the command
				if(history.substr(0,10) == "INITIALIZE"){
					
					// create a new user
					user* new_user = new user();
					
					if(users_db.find(history.substr(11)) == users_db.end()){
						// add to the database and all users list
						users_db.insert(std::pair<std::string, user*>(history.substr(11), new_user));
						all_users.push_back(history.substr(11));
						
						// add the user to their own followers and following
						users_db.at(history.substr(11))->followers.push_back(history.substr(11));
						users_db.at(history.substr(11))->following.push_back(history.substr(11));
						
						
						// add to initialized users
						initialized_users.push_back(history.substr(11));
					}
				}
				else if(history.substr(0,6) == "FOLLOW"){
					// get the user requesting the follow
					// and the requested user
					std::size_t index = history.find_first_of("|");
					std::string requesting_user = history.substr(7,index - 7);
					std::string requested_user = history.substr(index+1);
				
					// add the requested user to requesting's following
					users_db.at(requesting_user)->following.push_back(requested_user);
				
					// add the requesting user to the requested's followers
					users_db.at(requested_user)->followers.push_back(requesting_user);

					// add requested user's posts to the requesting's timeline
					if(!users_db.at(requested_user)->posts.empty()){
						int i = 0;
						int num_posts = users_db.at(requested_user)->posts.size();
						while(i != num_posts){
							if(users_db.at(requesting_user)->timeline.size() == 20){
								users_db.at(requesting_user)->timeline.pop();
							}
							users_db.at(requesting_user)->timeline.push(
									users_db.at(requested_user)->posts.at(num_posts - (i+1)));
							i++;				
						}
					}
				}
				else if(history.substr(0,8) == "UNFOLLOW"){
					// get the requesting and requested usernames
					std::size_t index = history.find_first_of("|");
					std::string requesting_user = history.substr(9,index - 9);
					std::string requested_user = history.substr(index+1);
				
					// remove the requesting from the requested's followers
					int position_to_remove1 = find_follower(users_db.at(requesting_user)->following, requested_user);
					int position_to_remove2 = find_follower(users_db.at(requested_user)->followers, requesting_user);
					users_db.at(requesting_user)->following.erase(users_db.at(requesting_user)->
												following.begin() + position_to_remove1);
					
					users_db.at(requested_user)->followers.erase(users_db.at(requested_user)->
												followers.begin() + position_to_remove2);

				}
				else if(history.substr(0,4) == "POST"){
					// construct the post
					std::vector<std::string> post_info;
					std::size_t index_user = history.find_first_of("|");
					std::string user = history.substr(5, index_user - 5);
					std::string rest_of_post = history.substr(index_user+1);
					std::size_t index_time = rest_of_post.find_first_of("|");
					std::string time = rest_of_post.substr(0, index_time);
					std::string content = rest_of_post.substr(index_time + 1);
					post_info.push_back(user);
					post_info.push_back(time);
					post_info.push_back(content);
					
					// add this post to the user's timeline and posts
					users_db.at(user)->timeline.push(post_info);
					users_db.at(user)->posts.push_back(post_info);
					
					// add the post to all of the user's followers
					std::vector<std::string> user_followers = users_db.at(user)->followers;
					if(!user_followers.empty()){
						for(int i = 0; i < user_followers.size(); i++){
							// don't add the post to the user's timeline again
							if(user_followers.at(i) != user){
								// if the user has 20 posts in their timeline pop the oldest one
								if(users_db.at(user_followers.at(i))->timeline.size() == 20){
									users_db.at(user_followers.at(i))->timeline.pop();
								}
								// add post to the timeline
								users_db.at(user_followers.at(i))->timeline.push(post_info);
							}
						}
					}
				
				}
			}
		}
		// clear the file to be written again
		old_log_file.close();
		return initialized_users;
	}

	// service that will be used to track if a process (client or slave is online)
	Status Ping(ServerContext* context, ServerReaderWriter<available_status, available_status>* stream) override {
		available_status on;
		on.set_available(1);
		while(1) {
			// write to the client or slave every 2 seconds
			// notifying that this server is online
			stream->Write(on);
			sleep(2);
			available_status received;
			received.set_available(0);
			stream->Read(&received);
			// if no message was received from the slave process, restart a new slave
			if(received.available() != 1){
				new_slave();
				break;
			}
			
		}
		return Status::OK;
	}
	
	public:
	// function that will build and run the server
	// public because main needs to call this function
	void run_server(std::string hostname, std::string port_no) {
		// Before building the server, restore the previous users
		std::vector<std::string> initialized_users = restore_server();
		
		// add the initialized users as the first commands in the server log
		new_log_file.open("new_server_log.txt");
		if(!new_log_file.is_open()){
			std::cout<<"could not open server log:"<<std::endl;
			std::exit(0);
		}
		
		// add all initialized users to the log file so they can be maintained through
		// future server logs
		if(!initialized_users.empty()){
			for(int i = 0; i < initialized_users.size(); i++){
				new_log_file << "INITIALIZE " + initialized_users.at(i) + "\n";
			}
		}
		
		// build and run the server on local host
		ServerBuilder builder;
		std::string connection_name = hostname + ":" + port_no;
	    	builder.AddListeningPort(connection_name, grpc::InsecureServerCredentials());
	
	    	
	    	builder.RegisterService(this);
	    	
	    	// Finally we can assemble the server.
	    	std::unique_ptr<Server> server(builder.BuildAndStart());
	    	
	    	// Wait for the server to shutdown.
	    	server->Wait();
		
	}
};

// function that will catch ctrl C
// will close log file
void handle_server_close(int p){
	std::cout<<"closing server"<<std::endl;
	new_log_file.close();
	std::exit(0);
}

int main(int argc, char** argv) {
	int opt = 0;
	bool router_exists = 0;
	bool ip_exists = 0;
	bool port_exists = 0;
	// get port number from the user
	while ((opt = getopt(argc, argv, "p:i:r:")) != -1){
		switch(opt) {
		    case 'p':{
			std::string temp_p(optarg);
			port = temp_p;
			port_exists = 1;
			break;
		    }
		    case 'i':{
			std::string temp_i(optarg);
			ipAddr = temp_i;
			ip_exists = 1;
			break;
		    }
		    case 'r':{
			std::string temp_r(optarg);
			router = temp_r;
			router_exists = 1;
			break;
		    }
		    default:{
			std::cerr << "Invalid Command Line Argument\n";
		    }
		}
	}
	// if command line args were not provided get the server ip/port and router ip/port from the user
	if(!ip_exists){
		std::cout << "Please enter the ip address of this machine in the form ###.###.###.###:####" << std::endl;
		std::cin >> ipAddr;
	}
	if(!port_exists){
		std::cout << "Please enter the port number you wish to use on this machine" << std::endl;
		std::cin >> port;
	}
	if(!router_exists){
		std::cout << "Please enter the router ip address and port number in the form <ip>:<port> (ie. ###.###.###.###:####)" << std::endl;
		std::cin >> router;
	}
	
	// create a new child/slave process
	if(fork() == 0){
		// use grpc to read and write messages to the master process
		
		// if the messages stop coming from master, restart program with exec
		// the exec process will have the same pid as this slave
		// while(Read) -> send message back
		std::string connection_name= ipAddr + ":" + port;
		
		std::unique_ptr<user_services::Stub> slave_stub(user_services::NewStub(grpc::CreateChannel(connection_name, grpc::InsecureChannelCredentials())));
		
		ClientContext context;
		// set up a Ping connection with the master process
		std::shared_ptr<ClientReaderWriter<available_status, available_status>> stream(
            slave_stub->Ping(&context));
		signal(SIGINT, SIG_IGN);
		available_status on;
		on.set_available(1);
		
		while(1){
			// write to the master process every 2 seconds
			stream->Write(on);
			sleep(2);
			available_status received;
			received.set_available(0);
			stream->Read(&received);
			// if no message was read from the master, start a new server
			// exec will create a new server process in place of the slave process
			if(received.available() != 1){
				sleep(2);
				execlp("./tsd","./tsd", "-i", ipAddr.c_str(), "-p", port.c_str(), "-r", router.c_str(), (char*) NULL);
			}
		}
	}
	else{ // master process
		
		signal(SIGINT, handle_server_close);
		// thread that will run the main server processes
		std::thread master_server([]() {
			TNSServiceImpl server;
			server.run_server(ipAddr, port);
		});
		// thread that will provide contact to the router, letting it know it is online
		std::thread router_contact([]() {
			// act as a client and send requests to router server
			// use grpc to read and write messages to the master process
		
			// if the messages stop coming from master, restart program with exec
			// the exec process will have the same pid as this slave
			// while(Read) -> send message back
			std::unique_ptr<user_services::Stub> slave_stub(user_services::NewStub(grpc::CreateChannel(router, grpc::InsecureChannelCredentials())));
		
			ClientContext context;
			std::shared_ptr<ClientReaderWriter<available_server, available_server>> stream(
		    slave_stub->UpdateRouter(&context));
		
			available_server on;
			on.set_ip_addr(ipAddr);
			on.set_port(port);
			on.set_online(1);
			
			while(1){
				stream->Write(on);
				sleep(2);
			}
		});
		master_server.join();
		router_contact.join();
	}	
}
