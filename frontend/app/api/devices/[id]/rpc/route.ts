import { NextResponse } from 'next/server';
import { sendRpcCommand } from '@/lib/thingsboard';

export const dynamic = 'force-dynamic';

export async function POST(
  request: Request,
  { params }: { params: Promise<{ id: string }> }
) {
  try {
    const { id } = await params;
    if (!id) {
      return NextResponse.json({ success: false, error: 'Device ID is required' }, { status: 400 });
    }

    const body = await request.json();
    const { method, params: rpcParams } = body;

    if (!method) {
      return NextResponse.json({ success: false, error: 'RPC method is required' }, { status: 400 });
    }

    const rpcResult = await sendRpcCommand(id, method, rpcParams || {});
    return NextResponse.json(rpcResult);
  } catch (error: any) {
    console.error(`API /api/devices/[id]/rpc error:`, error);
    return NextResponse.json(
      { success: false, error: error.message || 'Failed to dispatch RPC command' },
      { status: 500 }
    );
  }
}
