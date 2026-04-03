// Setup type definitions for built-in Supabase Runtime APIs
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.47.0";
import "jsr:@supabase/functions-js/edge-runtime.d.ts";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Authorization, x-api-key",
};

interface UploadResponse {
  success: boolean;
  message: string;
  image_upload_id?: string;
  detection_result_id?: string;
  error?: string;
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return new Response(
      JSON.stringify({
        success: false,
        error: "Method not allowed",
      }),
      {
        status: 405,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      },
    );
  }

  try {
    console.log(`[${new Date().toISOString()}] Received ${req.method} request`);

    const supabaseUrl = Deno.env.get("SUPABASE_URL");
    const supabaseKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
    const deviceApiKey = Deno.env.get("DEVICE_API_KEY");

    if (!supabaseUrl || !supabaseKey) {
      throw new Error(
        "Missing SUPABASE_URL or SUPABASE_SERVICE_ROLE_KEY environment variables",
      );
    }

    const supabase = createClient(supabaseUrl, supabaseKey);

    // Parse query parameters from URL
    const url = new URL(req.url);
    const params = url.searchParams;

    // Metadata from query parameters (fallback to headers for compatibility)
    const trapId = params.get("trap_id") || req.headers.get("x-trap-id");
    const capturedAt =
      params.get("captured_at") || req.headers.get("x-captured-at");
    const gpsLat = params.get("gps_lat") || req.headers.get("x-gps-lat");
    const gpsLon = params.get("gps_lon") || req.headers.get("x-gps-lon");
    const ldrValueStr =
      params.get("ldr_value") || req.headers.get("x-ldr-value");
    const isFallenStr =
      params.get("is_fallen") || req.headers.get("x-is-fallen");
    const batteryVoltageStr =
      params.get("battery_voltage") || req.headers.get("x-battery-voltage");
    const apiKey = params.get("api_key") || req.headers.get("x-api-key");
    const contentType = req.headers.get("content-type") || "image/jpeg";

    // Parse numeric values
    const ldrValue = ldrValueStr ? Number(ldrValueStr) : null;
    const isFallen = isFallenStr === "true";
    const batteryVoltage = batteryVoltageStr ? Number(batteryVoltageStr) : null;
    const gpsLatNum = gpsLat ? Number(gpsLat) : null;
    const gpsLonNum = gpsLon ? Number(gpsLon) : null;

    console.log(
      `Parsed metadata: trapId=${trapId}, capturedAt=${capturedAt}, contentType=${contentType}`,
    );

    // Optional simple device auth
    if (deviceApiKey && apiKey !== deviceApiKey) {
      return new Response(
        JSON.stringify({
          success: false,
          error: "Unauthorized",
        }),
        {
          status: 401,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    // Validations
    if (!trapId || !capturedAt) {
      return new Response(
        JSON.stringify({
          success: false,
          error: "Missing required parameters: trap_id or captured_at",
        }),
        {
          status: 400,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    if (!contentType.startsWith("image/")) {
      return new Response(
        JSON.stringify({
          success: false,
          error: `Unsupported content type: ${contentType}`,
        }),
        {
          status: 415,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    const imageBuffer = await req.arrayBuffer();

    if (!imageBuffer || imageBuffer.byteLength === 0) {
      return new Response(
        JSON.stringify({
          success: false,
          error: "Empty request body",
        }),
        {
          status: 400,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    // Step 1: Validate trap exists
    console.log(`[STEP 1] Validating trap: ${trapId}`);
    const { data: trap, error: trapError } = await supabase
      .from("traps")
      .select("id, trap_id")
      .eq("trap_id", trapId)
      .single();

    if (trapError) {
      console.error(`[STEP 1] Trap validation error: ${trapError.message}`);
    }

    if (!trap) {
      return new Response(
        JSON.stringify({
          success: false,
          error: `Trap not found: ${trapId}`,
        }),
        {
          status: 404,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    // Step 2: Upload raw image body to Supabase Storage
    console.log(`[STEP 2] Uploading image to storage...`);

    const timestamp = Date.now();
    const ext =
      contentType === "image/png"
        ? "png"
        : contentType === "image/webp"
          ? "webp"
          : "jpg";
    const imageFilename = `${trapId}-${timestamp}.${ext}`;

    // IMPORTANT: path inside the bucket, not including bucket name
    const imagePath = `${trapId}/${imageFilename}`;

    const { error: storageError } = await supabase.storage
      .from("trap-images")
      .upload(imagePath, imageBuffer, {
        contentType,
        upsert: false,
      });

    if (storageError) {
      console.error(`[STEP 2] Storage upload error: ${storageError.message}`);
      return new Response(
        JSON.stringify({
          success: false,
          error: `Failed to upload image: ${storageError.message}`,
        }),
        {
          status: 500,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    console.log(
      `[STEP 2] Image uploaded successfully: ${imagePath} (${imageBuffer.byteLength} bytes)`,
    );

    // Step 3: Insert row into image_uploads table
    const { data: imageUpload, error: imageUploadError } = await supabase
      .from("image_uploads")
      .insert({
        trap_id: trapId,
        captured_at: capturedAt,
        gps_lat: Number.isFinite(gpsLatNum) ? gpsLatNum : null,
        gps_lon: Number.isFinite(gpsLonNum) ? gpsLonNum : null,
        ldr_value: Number.isFinite(ldrValue) ? ldrValue : null,
        is_fallen: isFallen,
        battery_voltage: Number.isFinite(batteryVoltage)
          ? batteryVoltage
          : null,
        image_path: imagePath,
        image_filename: imageFilename,
        image_size_bytes: imageBuffer.byteLength,
        content_type: contentType,
        upload_status: "uploaded",
      })
      .select("id")
      .single();

    if (imageUploadError) {
      return new Response(
        JSON.stringify({
          success: false,
          error: `Failed to insert image_uploads record: ${imageUploadError.message}`,
        }),
        {
          status: 500,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    // Step 4: Insert placeholder row into detection_results table
    const { data: detectionResult, error: detectionError } = await supabase
      .from("detection_results")
      .insert({
        image_upload_id: imageUpload.id,
        beetle_count: 0,
        male_count: 0,
        female_count: 0,
        unknown_count: 0,
        classification_label: null,
        confidence_score: null,
        model_name: null,
        model_version: null,
        inference_time_ms: null,
        remarks: "Pending processing",
      })
      .select("id")
      .single();

    if (detectionError) {
      console.error("Detection results insertion warning:", detectionError);
      // Do not fail the request if placeholder insert fails
    }

    const response: UploadResponse = {
      success: true,
      message: "Raw image uploaded successfully",
      image_upload_id: imageUpload.id,
      detection_result_id: detectionResult?.id,
    };

    return new Response(JSON.stringify(response), {
      status: 200,
      headers: { ...corsHeaders, "Content-Type": "application/json" },
    });
  } catch (error) {
    console.error("Unexpected error:", error);

    return new Response(
      JSON.stringify({
        success: false,
        error: `Server error: ${
          error instanceof Error ? error.message : "Unknown error"
        }`,
      }),
      {
        status: 500,
        headers: { ...corsHeaders, "Content-Type": "application/json" },
      },
    );
  }
});

/*
Example request shape (using query parameters for A7670 compatibility):

POST /functions/v1/upload-trap-image2?trap_id=TRAP-001&captured_at=2026-03-08T20%3A15%3A00Z&gps_lat=9.771234&gps_lon=124.472134&ldr_value=12&is_fallen=false&battery_voltage=3.91&api_key=YOUR_DEVICE_SECRET
Content-Type: image/jpeg

[BINARY JPEG BODY]

curl example:

curl -i --location --request POST 'http://127.0.0.1:54321/functions/v1/upload-trap-image2?trap_id=TRAP-001&captured_at=2026-03-08T20%3A15%3A00Z&gps_lat=9.771234&gps_lon=124.472134&ldr_value=12&is_fallen=false&battery_voltage=3.91&api_key=YOUR_DEVICE_SECRET' \
  --header 'Content-Type: image/jpeg' \
  --data-binary '@/path/to/image.jpg'
*/
