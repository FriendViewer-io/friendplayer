import socket
import string
import time
import threading
import random
import sys
import puncher_messages_pb2 as proto

host_sessions = {}

HOST = '0.0.0.0'

BUFFER_SIZE = 1024
SESSION_TIMEOUT_SECS = 10

def heartbeat_killer():
    while True: 
        for _, session in list(host_sessions.items()):
            if time.time() - session["last_heartbeat"] > SESSION_TIMEOUT_SECS:
                del host_sessions[session["host_name"]]
                print("Removing host session for {}".format(session["host_name"]))
        time.sleep(1)


def id_generator(size=6, chars=string.ascii_letters + string.digits + string.punctuation):
    return ''.join(random.choice(chars) for _ in range(size))

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: ./puncher.py PORT")
        sys.exit(1)

    port = int(sys.argv[1])
    
    heartbeat_thread = threading.Thread(target=heartbeat_killer)
    heartbeat_thread.start()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, port))

        while(True):
            
            proto_bytes, conn_addr = s.recvfrom(BUFFER_SIZE)
            connect_msg = proto.ConnectMessage()
            connect_msg.ParseFromString(proto_bytes)

            #register the host
            if connect_msg.HasField("host"):
                host_connect_msg = connect_msg.host
                host_name = host_connect_msg.host_name
                if host_name in host_sessions:
                    print("Host {} already exists in host session".format(host_name))
                    response = proto.ServerMessage()
                    response.host.succeeded = False
                    response.host.token = ""
                    s.sendto(response.SerializeToString(), conn_addr)
                    continue
                host_sessions[host_name] = {}
                host_sessions[host_name]["host_name"] = host_name
                host_sessions[host_name]["ip"] = conn_addr[0]
                host_sessions[host_name]["port"] = conn_addr[1]
                host_sessions[host_name]["last_heartbeat"] = time.time()
                if host_connect_msg.HasField("password"):
                    host_sessions[host_name]["password"] = host_connect_msg.password
                else:
                    host_sessions[host_name]["password"] = ""
                
                host_sessions[host_name]["token"] = id_generator(size=32)
                print("Recieved host registration, hostname={}, token={}".format(host_name, host_sessions[host_name]["token"]))
                response = proto.ServerMessage()
                response.host.token = host_sessions[host_name]["token"]
                response.host.succeeded = True
                s.sendto(response.SerializeToString(), conn_addr)
                
            # get host data for client
            elif connect_msg.HasField("client"):
                client_connect_msg = connect_msg.client
                host_name = client_connect_msg.host_name
                if host_name not in host_sessions:
                    print("Client asked for unknown host {}".format(host_name))
                    response = proto.ServerMessage()
                    response.client.succeeded = False
                    s.sendto(response.SerializeToString(), conn_addr)
                    continue
                host_session = host_sessions[host_name]
                if len(host_session["password"]) > 0 and (not client_connect_msg.HasField("password") or host_session["password"] != client_connect_msg.password):
                    print("Wrong password provided for connection")
                    response = proto.ServerMessage()
                    response.client.succeeded = False
                    s.sendto(response.SerializeToString(), conn_addr)
                    continue
                
                response = proto.ServerMessage()
                response.new_client.ip = conn_addr[0]
                response.new_client.port = conn_addr[1]
                print("Sending client info to host")
                s.sendto(response.SerializeToString(), (host_session["ip"], host_session["port"]))

                response = proto.ServerMessage()
                response.client.succeeded = True
                response.client.host_token = host_session["token"]
                response.client.ip = host_session["ip"]
                response.client.port = host_session["port"]
                print("Sending back host info to client")
                s.sendto(response.SerializeToString(), conn_addr)
            
            elif connect_msg.HasField("heartbeat"):
                heartbeat_msg = connect_msg.heartbeat
                session = host_sessions[heartbeat_msg.host_name]
                if session["token"] == heartbeat_msg.token:
                    print("Heartbeat received from host {}".format(heartbeat_msg.host_name))
                    session["last_heartbeat"] = time.time()
            
            elif connect_msg.HasField("exit"):
                exit_msg = connect_msg.exit
                for _, session in list(host_sessions.items()):
                    if session["host_name"] == exit_msg.host_name and session["token"] == exit_msg.token:
                        del host_sessions[session["host_name"]]
                        print("Exiting host session for {}".format(session["host_name"]))