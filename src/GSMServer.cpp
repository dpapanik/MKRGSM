#include "GSMServer.h"

enum {
  SERVER_STATE_IDLE,
  SERVER_STATE_CREATE_SOCKET,
  SERVER_STATE_WAIT_CREATE_SOCKET_RESPONSE,
  SERVER_STATE_LISTEN,
  SERVER_STATE_WAIT_LISTEN_RESPONSE,
  SERVER_STATE_CLOSE_SOCKET,
  SERVER_STATE_WAIT_CLOSE_SOCKET
};


GSMServer::GSMServer(uint16_t port, bool synch) :
  _port(port),
  _synch(synch),
  _socket(-1),
  _state(SERVER_STATE_IDLE)
{
  for (int i = 0; i < MAX_CHILD_SOCKETS; i++) {
    _childSockets[i].socket = -1;
    _childSockets[i].accepted = false;
  }

  MODEM.addUrcHandler(this);
}

GSMServer::~GSMServer()
{
  MODEM.removeUrcHandler(this);
}

int GSMServer::ready()
{
  int ready = MODEM.ready();

  if (ready == 0) {
    return 0;
  }

  switch (_state) {
    case SERVER_STATE_IDLE:
    default: {
      break;
    }

    case SERVER_STATE_CREATE_SOCKET: {
      MODEM.setResponseDataStorage(&_response);
      MODEM.send("AT+USOCR=6");

      _state = SERVER_STATE_WAIT_CREATE_SOCKET_RESPONSE;
      ready = 0;
      break;
    }

    case SERVER_STATE_WAIT_CREATE_SOCKET_RESPONSE: {
      if (ready > 1 || !_response.startsWith("+USOCR: ")) {
        _state = SERVER_STATE_IDLE;
      } else {
        _socket = _response.charAt(_response.length() - 1) - '0';

        _state = SERVER_STATE_LISTEN;
        ready = 0;
      }
      break;
    }

    case SERVER_STATE_LISTEN: {
      String command;

      command.reserve(16);

      command += "AT+USOLI=";
      command += _socket;
      command += ",";
      command += _port;

      MODEM.send(command);

      _state = SERVER_STATE_WAIT_LISTEN_RESPONSE;
      ready = 0;
      break;
    }

    case SERVER_STATE_WAIT_LISTEN_RESPONSE: {
      if (ready > 1) {
        _state = SERVER_STATE_CLOSE_SOCKET;
        ready = 0;
      } else {
        _state = SERVER_STATE_IDLE;
      }
      break;
    }

    case SERVER_STATE_CLOSE_SOCKET: {
      String command;
      command.reserve(10);

      command += "AT+USOCL=";
      command += _socket;

      MODEM.send(command);

      _state = SERVER_STATE_WAIT_CLOSE_SOCKET;
      ready = 0;
      break;
    }

    case SERVER_STATE_WAIT_CLOSE_SOCKET: {
      _state = SERVER_STATE_IDLE;
      _socket = -1;
      break;
    }
  }

  return ready;
}

void GSMServer::begin()
{
  _state = SERVER_STATE_CREATE_SOCKET;

  if (_synch) {
    while (ready() == 0);
  }
}

GSMClient GSMServer::available(bool synch)
{
  MODEM.poll();

  int socket = -1;

  if (_socket != -1) {
    // search for new accepted sockets first
    for (int i = 0; i < MAX_CHILD_SOCKETS; i++) {
      if (_childSockets[i].socket != -1 && _childSockets[i].accepted) {
        _childSockets[i].accepted = false;
        socket = _childSockets[i].socket;

        break;
      }
    }

    if (socket == -1) {
      // no new accepted sockets, search for one with data to be read
      for (int i = 0; i < MAX_CHILD_SOCKETS; i++) {
        if (_childSockets[i].socket != -1) {
          GSMClient client(_childSockets[i].socket, true);

          if (client.available()) {
            socket = _childSockets[i].socket;
            break;
          }
        }
      }
    }
  }

  return GSMClient(socket, synch);
}

void GSMServer::beginWrite()
{
}

size_t GSMServer::write(uint8_t c)
{
  return write(&c, sizeof(c));
}

size_t GSMServer::write(const uint8_t *buf)
{
  return write(buf, strlen((const char*)buf));
}

size_t GSMServer::write(const uint8_t *buf, size_t sz)
{
  size_t written = 0;

  if (_socket != -1) {
    for (int i = 0; i < MAX_CHILD_SOCKETS; i++) {
      if (_childSockets[i].socket != -1) {
        GSMClient client(_childSockets[i].socket, true);

        written += client.write(buf, sz);
      }
    }
  }

  return written;
}

void GSMServer::endWrite()
{
}

void GSMServer::stop()
{
  if (_socket < 0) {
    return;
  }

  String command;
  command.reserve(10);

  command += "AT+USOCL=";
  command += _socket;

  MODEM.send(command);
  MODEM.waitForResponse(10000);

  _socket = -1;
}

void GSMServer::handleUrc(const String& urc)
{
  if (urc.startsWith("+UUSOLI: ")) {
    int socket = urc.charAt(9) - '0';

    for (int i = 0; i < MAX_CHILD_SOCKETS; i++) {
      if (_childSockets[i].socket == -1) {
        _childSockets[i].socket = socket;
        _childSockets[i].accepted = true;

        break;
      }
    }
  } else if (urc.startsWith("+UUSOCL: ")) {
    int socket = urc.charAt(urc.length() - 1) - '0';

    for (int i = 0; i < MAX_CHILD_SOCKETS; i++) {
      if (_childSockets[i].socket == socket) {
        _childSockets[i].socket = -1;
        _childSockets[i].accepted = false;

        break;
      }
    }
  }
}
