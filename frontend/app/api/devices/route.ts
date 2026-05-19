import { NextResponse } from 'next/server';
import { getDevices } from '@/lib/thingsboard';

export const dynamic = 'force-dynamic';

export async function GET() {
  try {
    const rawDevices = await getDevices();
    
    // Clean and map the device list to return only what the frontend needs
    const devices = (rawDevices.data || []).map((dev: any) => ({
      id: dev.id.id,
      name: dev.name,
      type: dev.type,
      label: dev.label || '',
    }));

    return NextResponse.json({ success: true, devices });
  } catch (error: any) {
    console.error('API /api/devices error:', error);
    return NextResponse.json(
      { success: false, error: error.message || 'Failed to fetch devices' },
      { status: 500 }
    );
  }
}
