namespace Kerberos.Core
{
    /// <summary>
    /// Game UI helpers shared by all gameplay scripts.
    /// </summary>
    public static class UI
    {
        /// <summary>
        /// True while a modal UI canvas is visible or a text field owns the keyboard.
        /// Gameplay scripts should skip camera/character input while this is true.
        /// </summary>
        public static bool InputCaptured => InternalCalls.UI_IsInputCaptured();
    }
}
