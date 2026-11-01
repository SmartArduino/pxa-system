// SPDX-License-Identifier: MIT
// A browser has no raw TCP/TLS socket API. Do not tunnel sockets via a server.
// Signed optional permissions remain distinct from operation availability.
export function installBrowserSockets(host,permissions=[]){
  host.browserSocketAvailable=false;
  host.browserSocketDeclared=permissions.some(p=>p.name==='net.socket'&&p.scope==='outbound');
  return {close(){}};
}
