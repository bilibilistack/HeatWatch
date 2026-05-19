import { NextResponse } from 'next/server';
import type { NextRequest } from 'next/server';

export function middleware(request: NextRequest) {
  // Get request origin
  const origin = request.headers.get('origin') || '';
  
  // Permitted origins list including localhost and 127.0.0.1 (various ports just in case)
  const allowedOrigins = [
    'http://localhost:3000',
    'http://127.0.0.1:3000',
    'http://localhost:3001',
    'http://127.0.0.1:3001',
    'http://localhost:8000',
    'http://127.0.0.1:8000',
  ];

  const isAllowed = allowedOrigins.includes(origin);

  // Handle preflight OPTIONS requests
  if (request.method === 'OPTIONS') {
    const preflightHeaders = {
      'Access-Control-Allow-Origin': isAllowed ? origin : allowedOrigins[0],
      'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type, Authorization, X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Date',
      'Access-Control-Allow-Credentials': 'true',
      'Access-Control-Max-Age': '86400',
    };
    return new NextResponse(null, {
      status: 204,
      headers: preflightHeaders,
    });
  }

  // Handle normal requests
  const response = NextResponse.next();
  
  if (isAllowed) {
    response.headers.set('Access-Control-Allow-Origin', origin);
  } else {
    // If not in standard list, allow localhost/127.0.0.1 fallback
    response.headers.set('Access-Control-Allow-Origin', 'http://127.0.0.1:3000');
  }
  
  response.headers.set('Access-Control-Allow-Credentials', 'true');
  response.headers.set('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
  response.headers.set('Access-Control-Allow-Headers', 'Content-Type, Authorization, X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Date');

  return response;
}

// App routing matcher to intercept only api routes
export const config = {
  matcher: '/api/:path*',
};
