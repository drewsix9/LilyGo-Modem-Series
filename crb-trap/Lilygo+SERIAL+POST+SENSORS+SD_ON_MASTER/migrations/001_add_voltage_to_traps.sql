-- Migration: Add voltage columns to traps table
-- Purpose: Track latest battery and solar voltage readings per trap

ALTER TABLE public.traps
ADD COLUMN IF NOT EXISTS battery_voltage double precision,
ADD COLUMN IF NOT EXISTS solar_voltage numeric,
ADD COLUMN IF NOT EXISTS last_voltage_update timestamp with time zone DEFAULT now();

-- Create index for efficient updates
CREATE INDEX IF NOT EXISTS idx_traps_trap_id ON public.traps(trap_id);
