namespace Kerberos
{
    /// <summary>
    /// Runtime graphics settings exposed to gameplay scripts.
    /// </summary>
    public static class Graphics
    {
        public static bool IsRayTracingSupported => InternalCalls.Graphics_IsRayTracingSupported();

        public static bool RayQueryShadows
        {
            get => InternalCalls.Graphics_GetRayQueryShadows();
            set
            {
                if (IsRayTracingSupported)
                    InternalCalls.Graphics_SetRayQueryShadows(value);
            }
        }

        public static bool RayQuerySoftShadows
        {
            get => InternalCalls.Graphics_GetRayQuerySoftShadows();
            set
            {
                if (IsRayTracingSupported)
                    InternalCalls.Graphics_SetRayQuerySoftShadows(value);
            }
        }

        public static bool RayQueryAmbientOcclusion
        {
            get => InternalCalls.Graphics_GetRayQueryAmbientOcclusion();
            set
            {
                if (IsRayTracingSupported)
                    InternalCalls.Graphics_SetRayQueryAmbientOcclusion(value);
            }
        }

        public static bool Gtao
        {
            get => InternalCalls.Graphics_GetGtao();
            set => InternalCalls.Graphics_SetGtao(value);
        }

        public static bool PointShadows
        {
            get => InternalCalls.Graphics_GetPointShadows();
            set => InternalCalls.Graphics_SetPointShadows(value);
        }

        public static bool Pcf
        {
            get => InternalCalls.Graphics_GetPcf();
            set => InternalCalls.Graphics_SetPcf(value);
        }

        public static float ExposureCompensation
        {
            get => InternalCalls.Graphics_GetExposureCompensation();
            set => InternalCalls.Graphics_SetExposureCompensation(value);
        }

        /// <summary>0 = None, 1 = FXAA, 2 = SMAA, 3 = TAA (not implemented).</summary>
        public static int AntiAliasing
        {
            get => InternalCalls.Graphics_GetAntiAliasing();
            set => InternalCalls.Graphics_SetAntiAliasing(value);
        }
    }
}
