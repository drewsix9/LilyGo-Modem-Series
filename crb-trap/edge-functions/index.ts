// Setup type definitions for built-in Supabase Runtime APIs
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.47.0";
import "jsr:@supabase/functions-js/edge-runtime.d.ts";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
  "Access-Control-Allow-Headers":
    "Content-Type, Authorization, x-api-key, x-image-crc32",
};

interface UploadResponse {
  success: boolean;
  message: string;
  image_upload_id?: string;
  detection_result_id?: string;
  expected_crc32?: string;
  computed_crc32?: string;
  error?: string;
}

// CRC32 (IEEE 802.3 / poly 0xEDB88320) - same algorithm used in your firmware
function calculateCRC32(bytes: Uint8Array): number {
  let crc = 0xffffffff;

  for (let i = 0; i < bytes.length; i++) {
    crc ^= bytes[i];
    for (let j = 0; j < 8; j++) {
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
  }

  return (crc ^ 0xffffffff) >>> 0;
}

function toHex8(v: number): string {
  return v.toString(16).toUpperCase().padStart(8, "0");
}

function normalizeCrcHex(input: string | null): string | null {
  if (!input) return null;
  const normalized = input.trim().toUpperCase().replace(/^0X/, "");
  return normalized;
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes)
    .map((b) => b.toString(16).toUpperCase().padStart(2, "0"))
    .join(" ");
}

function decodeBase64ToBytes(base64Text: string): Uint8Array {
  const binaryString = atob(base64Text);
  const out = new Uint8Array(binaryString.length);
  for (let i = 0; i < binaryString.length; i++) {
    out[i] = binaryString.charCodeAt(i);
  }
  return out;
}

Deno.serve(async (req: Request) => {
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
    console.log(`[${new Date().toISOString()}] Incoming upload request...`);

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
    let trapId = params.get("trap_id") || req.headers.get("x-trap-id");
    let capturedAt =
      params.get("captured_at") || req.headers.get("x-captured-at");
    let gpsLat = params.get("gps_lat") || req.headers.get("x-gps-lat");
    let gpsLon = params.get("gps_lon") || req.headers.get("x-gps-lon");
    let ldrValueStr = params.get("ldr_value") || req.headers.get("x-ldr-value");
    let isFallenStr = params.get("is_fallen") || req.headers.get("x-is-fallen");
    let batteryVoltageStr =
      params.get("battery_voltage") || req.headers.get("x-battery-voltage");
    let apiKey = params.get("api_key") || req.headers.get("x-api-key");
    let expectedCrc32 = normalizeCrcHex(
      params.get("image_crc32") || req.headers.get("x-image-crc32"),
    );
    const contentType =
      req.headers.get("content-type") || "application/octet-stream";
    const isMultipart = contentType
      .toLowerCase()
      .startsWith("multipart/form-data");

    console.log(
      `Parsed metadata: trapId=${trapId}, capturedAt=${capturedAt}, contentType=${contentType}, expectedCrc32=${expectedCrc32 ?? "none"}`,
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

    if (
      !isMultipart &&
      !contentType.startsWith("image/") &&
      contentType !== "application/octet-stream" &&
      contentType !== "text/plain"
    ) {
      return new Response(
        JSON.stringify({
          success: false,
          error: `Unsupported content type: ${contentType}. Use multipart/form-data, image/*, application/octet-stream, or text/plain`,
        }),
        {
          status: 415,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    if (!expectedCrc32) {
      return new Response(
        JSON.stringify({
          success: false,
          error: "Missing required CRC metadata: image_crc32 or x-image-crc32",
        }),
        {
          status: 400,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    if (!/^[0-9A-F]{8}$/.test(expectedCrc32)) {
      return new Response(
        JSON.stringify({
          success: false,
          error: "Invalid CRC32 format. Expected 8 hex chars, e.g. 243F5C18",
        }),
        {
          status: 400,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    // New firmware sends multipart with a file part.
    // Legacy firmware sends raw binary JPEG or base64 text body.
    let imageBytes: Uint8Array<ArrayBufferLike> = new Uint8Array(0);
    let effectiveContentType = contentType;

    if (isMultipart) {
      const form = await req.formData();

      const trapIdField = form.get("trap_id");
      const capturedAtField = form.get("captured_at");
      const gpsLatField = form.get("gps_lat");
      const gpsLonField = form.get("gps_lon");
      const ldrValueField = form.get("ldr_value");
      const isFallenField = form.get("is_fallen");
      const batteryVoltageField = form.get("battery_voltage");
      const apiKeyField = form.get("api_key");
      const crcField = form.get("image_crc32");

      if (!trapId && typeof trapIdField === "string") trapId = trapIdField;
      if (!capturedAt && typeof capturedAtField === "string") {
        capturedAt = capturedAtField;
      }
      if (!gpsLat && typeof gpsLatField === "string") gpsLat = gpsLatField;
      if (!gpsLon && typeof gpsLonField === "string") gpsLon = gpsLonField;
      if (!ldrValueStr && typeof ldrValueField === "string") {
        ldrValueStr = ldrValueField;
      }
      if (!isFallenStr && typeof isFallenField === "string") {
        isFallenStr = isFallenField;
      }
      if (!batteryVoltageStr && typeof batteryVoltageField === "string") {
        batteryVoltageStr = batteryVoltageField;
      }
      if (!apiKey && typeof apiKeyField === "string") apiKey = apiKeyField;
      if (!expectedCrc32 && typeof crcField === "string") {
        expectedCrc32 = normalizeCrcHex(crcField);
      }

      const uploaded = form.get("file") || form.get("image");
      if (!(uploaded instanceof File)) {
        return new Response(
          JSON.stringify({
            success: false,
            error: "Multipart payload is missing file/image field",
          }),
          {
            status: 400,
            headers: { ...corsHeaders, "Content-Type": "application/json" },
          },
        );
      }

      const fileBuffer = await uploaded.arrayBuffer();
      imageBytes = new Uint8Array(fileBuffer);
      effectiveContentType = uploaded.type || "image/jpeg";

      console.log(
        `[BODY] Multipart payload received: ${imageBytes.byteLength} bytes (${effectiveContentType})`,
      );
    } else {
      const rawBodyBuffer = await req.arrayBuffer();
      if (rawBodyBuffer.byteLength === 0) {
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

      imageBytes = new Uint8Array(rawBodyBuffer);
      effectiveContentType = contentType;

      if (contentType === "text/plain") {
        const rawText = new TextDecoder().decode(imageBytes).trim();
        try {
          imageBytes = decodeBase64ToBytes(rawText);
          effectiveContentType = "image/jpeg";
          console.log(
            `[BODY] Legacy base64 decode succeeded: ${rawText.length} chars -> ${imageBytes.byteLength} bytes`,
          );
        } catch (_error) {
          console.warn(
            `[BODY] text/plain payload is not valid base64; using raw text bytes (${imageBytes.byteLength} bytes)`,
          );
        }
      } else {
        console.log(
          `[BODY] Binary payload received: ${imageBytes.byteLength} bytes`,
        );
      }
    }

    // Parse numeric values after all metadata fallbacks are resolved.
    const ldrValue = ldrValueStr ? Number(ldrValueStr) : null;
    const isFallen = isFallenStr === "true";
    const batteryVoltage = batteryVoltageStr ? Number(batteryVoltageStr) : null;
    const gpsLatNum = gpsLat ? Number(gpsLat) : null;
    const gpsLonNum = gpsLon ? Number(gpsLon) : null;

    const imageBuffer = imageBytes.buffer as ArrayBuffer;

    if (imageBuffer.byteLength === 0) {
      return new Response(
        JSON.stringify({
          success: false,
          error: "Empty decoded image body",
        }),
        {
          status: 400,
          headers: { ...corsHeaders, "Content-Type": "application/json" },
        },
      );
    }

    // CRC check immediately after receiving payload
    const computedCrc32 = toHex8(calculateCRC32(imageBytes));

    const first16 = imageBytes.slice(0, Math.min(16, imageBytes.length));
    const last16 = imageBytes.slice(Math.max(0, imageBytes.length - 16));

    if (computedCrc32 !== expectedCrc32) {
      console.error(
        `[CRC ERROR] Mismatch! Expected: ${expectedCrc32}, Computed: ${computedCrc32}, Size: ${imageBuffer.byteLength}`,
      );
      console.info(`[DIAGNOSTIC] First 16 bytes: ${bytesToHex(first16)}`);
      console.info(`[DIAGNOSTIC] Last 16 bytes: ${bytesToHex(last16)}`);
      console.info(
        `[DIAGNOSTIC] Received size: ${imageBuffer.byteLength} bytes`,
      );
      console.warn(
        "[CRC] Continuing upload despite mismatch (diagnostic mode)",
      );
    } else {
      console.log(
        `[CRC] OK expected=${expectedCrc32} computed=${computedCrc32} bytes=${imageBuffer.byteLength}`,
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
      effectiveContentType === "image/png"
        ? "png"
        : effectiveContentType === "image/webp"
          ? "webp"
          : "jpg";
    const imageFilename = `${trapId}-${timestamp}.${ext}`;

    // IMPORTANT: path inside the bucket, not including bucket name
    const imagePath = `${trapId}/${imageFilename}`;

    const { error: storageError } = await supabase.storage
      .from("trap-images")
      .upload(imagePath, imageBuffer, {
        contentType:
          effectiveContentType === "application/octet-stream" ||
          effectiveContentType === "text/plain"
            ? "image/jpeg"
            : effectiveContentType,
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
        content_type: effectiveContentType,
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
      expected_crc32: expectedCrc32 ?? undefined,
      computed_crc32: computedCrc32,
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
Example request shape (A7670-compatible query metadata + CRC32):

POST /functions/v1/upload-trap-image2?trap_id=TRAP-001&captured_at=2026-03-08T20%3A15%3A00Z&gps_lat=9.771234&gps_lon=124.472134&ldr_value=12&is_fallen=false&battery_voltage=3.91&api_key=YOUR_DEVICE_SECRET&image_crc32=243F5C18
Content-Type: multipart/form-data; boundary=----ESP32_A7670_Boundary

------ESP32_A7670_Boundary
Content-Disposition: form-data; name="file"; filename="photo.jpg"
Content-Type: image/jpeg

[BINARY JPEG BODY]
------ESP32_A7670_Boundary--

Legacy request shape (still supported):

POST /functions/v1/upload-trap-image2?trap_id=TRAP-001&captured_at=2026-03-08T20%3A15%3A00Z&gps_lat=9.771234&gps_lon=124.472134&ldr_value=12&is_fallen=false&battery_voltage=3.91&api_key=YOUR_DEVICE_SECRET&image_crc32=243F5C18
Content-Type: image/jpeg

[BINARY JPEG BODY]
*/
