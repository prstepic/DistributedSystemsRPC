#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <thread>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "TNSService.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using TNSService::user_services;
using TNSService::command_info;
using TNSService::server_status;
using TNSService::current_user;
using TNSService::following_user_message;
using TNSService::post_info;

struct user {
	
	std::string username = "";
	std::vector<std::string> followers;
	std::vector<std::string> following;
	std::stack<std::vector<std::string>> timeline;
	std::vector<std::vector<std::string>> posts;
};

int find_follower(std::vector<std::string> v, std::string u){
	for(int i = 0; i < v.size(); i++){
		if(v.at(i) == u){
			return i;
		}
	}
	return -1;
}

std::unordered_map<std::string, user*> users_db;
std::vector<std::string> all_users;

class TNSServiceImpl final : public user_services::Service{

	Status InitializeUser(ServerContext* context, const current_user* request, server_status* response) override {
		std::cout<<"Initializing user"<<std::endl;
		// make sure the username doesn't already exist
		std::string requesting_user = request->username();
		std::cout<<"getting the username"<<std::endl;
		std::cout<<"username: "<<requesting_user<<std::endl;
		if(users_db.find(requesting_user) != users_db.end()){
			std::cout<<"username already exists"<<std::endl;
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_ALREADY_EXISTS);
		}
		else{
			std::cout<<"adding user"<<std::endl;
			user* user_to_insert = new user();
			user_to_insert->username = requesting_user;
			std::cout<<"username set"<<std::endl;
			users_db.insert(std::pair<std::string, user*>(requesting_user, user_to_insert));
			std::cout<<"user inserted into database"<<std::endl;
			all_users.push_back(requesting_user);
			std::cout<<"end else"<<std::endl;
		}
		response->set_s_status(TNSService::server_status_IStatus_SUCCESS);
		std::cout<<"returning status"<<std::endl;
		return Status::OK;
	}

	Status FollowRequest(ServerContext* context, const command_info* request, server_status* response) override {
		
		// get the user requesting a follow and the user that wants to be followed
		std::string requesting_user = request->username();
		std::string user_to_follow = request->username_other_user();
		std::cout<<requesting_user<<" trying to follow "<<user_to_follow<<std::endl;
		// make sure the requested user exists
		if(users_db.find(user_to_follow) == users_db.end()){
			std::cout<<"User not found"<<std::endl;
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_NOT_EXISTS);
		}

		// make sure the user isn't requesting to follow themselves
		else if(user_to_follow == requesting_user){
			std::cout<<"User is trying to follow themselves"<<std::endl;
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
					if(users_db.at(requesting_user)->timeline.size() == 20){
						users_db.at(requesting_user)->timeline.pop();
					}
					users_db.at(requesting_user)->timeline.push(
							users_db.at(user_to_follow)->posts.at(num_posts - (i+1)));
					i++;				
				}
			}
		}

		return Status::OK;
	}
	
	Status UnfollowRequest(ServerContext* context, const command_info* request, server_status* response) override {
		
		// get the user requesting a follow and the user that wants to be followed
		std::string requesting_user = request->username();
		std::string user_to_unfollow = request->username_other_user();

		// make sure the requested user exists
		if(users_db.find(user_to_unfollow) == users_db.end()){
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_NOT_EXISTS);
		}

		// make sure the user isn't requesting to follow themselves
		else if(user_to_unfollow == requesting_user){
			response->set_s_status(TNSService::server_status_IStatus_FAILURE_INVALID);
		}
		
		else{
			// remove the requested user to follow to the requesting user's following list
			// and remove the requesting user to the requested user's followers list
			int position_to_remove1 = find_follower(users_db.at(requesting_user)->following, user_to_unfollow);
			int position_to_remove2 = find_follower(users_db.at(user_to_unfollow)->followers, requesting_user);
			std::cout<<"removing follower at "<<position_to_remove1<<std::endl;
			std::cout<<"removing follower at "<<position_to_remove2<<std::endl;
			users_db.at(requesting_user)->following.erase(users_db.at(requesting_user)->following.begin() + position_to_remove1);
			std::cout<<"removal from following "<<std::endl;
			users_db.at(user_to_unfollow)->followers.erase(users_db.at(user_to_unfollow)->followers.begin() + position_to_remove2);
			std::cout<<"users removed from lists "<<std::endl;
			response->set_s_status(TNSService::server_status_IStatus_SUCCESS);
		}
		return Status::OK;
	}

	Status ListRequest(ServerContext* context, const current_user* request, ServerWriter<following_user_message>* writer) override {
		std::string user_making_request = request->username();

		// keep sending usernames from followers and all users
		// if the end of either list is sent, send "END" as the value in the message
		// all users should always be longer or equal than the followers of the user
		// make check, if it fails send invalid status 
		std::vector<std::string> user_followers = users_db.at(user_making_request)->followers;
		if(!user_followers.empty()){
			if(all_users.size() < user_followers.size()){
				following_user_message return_info;
				return_info.set_username("");
				return_info.set_user_in_all_users("");
				return_info.set_s_status(TNSService::following_user_message_IStatus_FAILURE_INVALID);
				writer->Write(return_info);
			}
			else{
				for(int i = 0; i < all_users.size(); i++){
					if(i > user_followers.size() - 1){
						following_user_message return_info;
						return_info.set_username("END");
						return_info.set_user_in_all_users(all_users.at(i));
						return_info.set_s_status(TNSService::following_user_message_IStatus_SUCCESS);
						writer->Write(return_info);
					}
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

		for(int i = 0; i < all_users.size(); i++){
			following_user_message return_info;
			return_info.set_username("END");
			return_info.set_user_in_all_users(all_users.at(i));
			return_info.set_s_status(TNSService::following_user_message_IStatus_SUCCESS);
			writer->Write(return_info);
		}
		
		
		return Status::OK;
	}

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
				std::string requesting_user = received_info.username();
				std::vector<std::string> user_followers = users_db.at(requesting_user)->followers;
				std::string post_time = received_info.time();
				std::string post_content = received_info.content();
				std::vector<std::string> post_info;
				post_info.push_back(requesting_user);
				post_info.push_back(post_time);
				post_info.push_back(post_content);
				users_db.at(requesting_user)->posts.push_back(post_info);
				if(!user_followers.empty()){
					for(int i = 0; i < user_followers.size(); i++){
						
						if(users_db.at(user_followers.at(i))->timeline.size() == 20){
							users_db.at(user_followers.at(i))->timeline.pop();
						}
						users_db.at(user_followers.at(i))->timeline.push(post_info);
					}
				}
			}
			// user is requesting an update to their timeline
			else{
				while(!users_db.at(received_info.username())->timeline.empty()){
					std::vector<std::string> timeline_info = users_db.at(received_info.username())->timeline.top();
					users_db.at(received_info.username())->timeline.pop();
					post_info updated_post;
					updated_post.set_username(timeline_info.at(0));
					updated_post.set_time(timeline_info.at(1));
					updated_post.set_content(timeline_info.at(2));
					stream->Write(updated_post);
				}
				post_info end_post;
				end_post.set_username("END");
				stream->Write(end_post);
			}
		}
		
		return Status::OK;
	}

	void restore_server(){

	}
	
	void add_to_log(std::string) {
		
	}
};


int main(int argc, char** argv) {
	std::string port = "3010";
	int opt = 0;
	while ((opt = getopt(argc, argv, "p:")) != -1){
		switch(opt) {
		    case 'p':
			port = optarg;break;
		    default:
			std::cerr << "Invalid Command Line Argument\n";
		}
	}
	ServerBuilder builder;
	std::string connection_name = "localhost:" + port;
    	builder.AddListeningPort(connection_name, grpc::InsecureServerCredentials());
	
    	std::cout<<"server now listening"<<std::endl;
    	TNSServiceImpl service;
    	builder.RegisterService(&service);
    	std::cout<<"server registered"<<std::endl;
    	// Finally we can assemble the server.
    	std::unique_ptr<Server> server(builder.BuildAndStart());
    
    	// Wait for the server to shutdown.
    	server->Wait();

}
