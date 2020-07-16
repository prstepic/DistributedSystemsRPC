#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <queue>
#include <thread>
#include <unistd.h>
#include <fstream>
#include <memory>
#include <signal.h>
#include <cstdio>
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
using TNSService::available_server;
using TNSService::client_info;

// variables that represent the current available server and
// the list of online servers
std::vector<std::string> available_server_info(2);
std::vector<std::vector<std::string>> online_servers;

// helper function to find a server in the list of online servers
int position_in_online_servers(std::string server_to_find, std::string port){
	for(int i = 0; i < online_servers.size(); i++){
		if(online_servers.at(i).at(0) == server_to_find && online_servers.at(i).at(1) == port){
			return i;
		}
	}
	return -1;
}

// Service class for the router server
class TNSServiceImpl final : public user_services::Service{

	// election function that will assign a new current available server
	void election(std::string dead_server, std::string port) {
		// if this election was triggered by the client, remove the server from the list of online servers
		
		int index = position_in_online_servers(dead_server, port);
		if(index != -1){
			
			online_servers.erase(online_servers.begin() + index);
		}
		
		// simple election algorithm that selects the next online server
		if(online_servers.size() > 0){
			available_server_info = online_servers.at(0);
		}
		else{
			available_server_info.at(0) = "ERROR";
		}
		
	}

	// service that provides the client with a new server ip and port
	Status RequestForServer(ServerContext* context, const client_info* request, available_server* response) override {
		
		// if the client is not currently connected to the available server
		if(request->ip_server() != available_server_info.at(0)) {
			response->set_ip_addr(available_server_info.at(0));
			response->set_port(available_server_info.at(1));
		}
		else { // this means the client sensed a disconnect before the router
			std::string dead_server = request->ip_server();
			std::string dead_port = request->port();
			// run election not including the dead server
			election(dead_server, dead_port);
			response->set_ip_addr(available_server_info.at(0));
			response->set_port(available_server_info.at(1));
		}
		
		return Status::OK;
	}

	// service the router will use to keep track of online servers
	Status UpdateRouter(ServerContext* context, ServerReaderWriter<available_server, available_server>* stream) override {
		// continually send messages to master servers that contain the current available server
		// continually read messages from the current available server
		// master servers will only add messages to the buffer if they are the available server
		// if the buffer is killed then the server is offline and the router needs to elect a new server
		available_server current_available_server;
		std::string server_contacted;
		std::string port_contacted;
		
		while(1){
			available_server received_info;
			received_info.set_online(0);
			stream->Read(&received_info);
			// make sure that a message was read from the stream from another server
			// servers will always send an online message of 1
			if(received_info.online() == 1) {
				server_contacted = received_info.ip_addr();
				port_contacted = received_info.port();
				// if this is a never seen before server add it to the list of online servers
				if(position_in_online_servers(server_contacted, port_contacted) == -1){
					// if this is the first server seen, set it as the current server
					if(online_servers.size() == 0){
						available_server_info.at(0) = server_contacted;
						available_server_info.at(1) = port_contacted;
					}
					std::vector<std::string> online_server;
					online_server.push_back(received_info.ip_addr());
					online_server.push_back(received_info.port());
					online_servers.push_back(online_server);
				}
				
			}
			
			// if a message wasn't received from the server elect a new master if this was the master's stream
			if(received_info.online() != 1){
				// if this was the master's stream
				if(server_contacted == available_server_info.at(0)){
					// remove the server from the list of online servers
					int index = position_in_online_servers(server_contacted, port_contacted);
					if(index != -1){
						
						online_servers.erase(online_servers.begin() + index);
					}
					
					election(server_contacted, port_contacted);
				}
				else{ // if the disconnected server was one of the standby servers
					int index = position_in_online_servers(server_contacted, port_contacted);
					if(index != -1){
						online_servers.erase(online_servers.begin() + index);
					}
				}
				
				break;
			}
			// notify the server of the current available server
			current_available_server.set_ip_addr(available_server_info.at(0));
			stream->Write(current_available_server);
			sleep(1);
		}
		return Status::OK;

	}
};

int main() {
	// send messages to other servers and get their responses
	// update available_server_info if need be
	// make sure that the interval that the router and client check if servers are online are consistent
	std::string router_port;
	std::string router_ip;
	
	// get the ip address and desired port of the router from the user
	std::cout << "Please enter the ip address of this machine (the router) in the form ###.###.###.###" << std::endl;
	std::cin >> router_ip;
	std::cout << "Please enter the port number you'd like to run the router on" << std::endl;
	std::cin >> router_port;

	

	// now launch the server to give the client the information to connect to the server initially
	// once the client gets info it will disconnect from this server and go to the available server
	ServerBuilder builder;
	builder.AddListeningPort((router_ip + ":" + router_port), grpc::InsecureServerCredentials());

	TNSServiceImpl service;
	builder.RegisterService(&service);

	// Finally we can assemble the server.
	std::unique_ptr<Server> server(builder.BuildAndStart());

	std::cout << "Server listening on " << (router_ip + ":" + router_port) << std::endl;

	// Wait for the server to shutdown.
	server->Wait();
}







