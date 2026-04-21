namespace Kerberos.Source.Kerberos.Core
{
    public static class Input
    {
        public static bool IsKeyDown(KeyCode keycode)
        {
            return InternalCalls.Input_IsKeyDown(keycode);
        }

        public static bool IsMouseButtonDown(MouseButton button)
        {
            return InternalCalls.Input_IsMouseButtonDown(button);
        }
    }
}