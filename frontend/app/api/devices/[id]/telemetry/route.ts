import { NextResponse } from 'next/server';
import { getLatestTelemetry } from '@/lib/thingsboard';

export const dynamic = 'force-dynamic';

export async function GET(
  request: Request,
  { params }: { params: Promise<{ id: string }> }
) {
  try {
    const { id } = await params;
    if (!id) {
      return NextResponse.json({ success: false, error: 'Device ID is required' }, { status: 400 });
    }

    const telemetry = await getLatestTelemetry(id);
    return NextResponse.json({ success: true, telemetry });
  } catch (error: any) {
    console.error(`API /api/devices/[id]/telemetry error:`, error);
    return NextResponse.json(
      { success: false, error: error.message || 'Failed to fetch telemetry' },
      { status: 500 }
    );
  }
}
