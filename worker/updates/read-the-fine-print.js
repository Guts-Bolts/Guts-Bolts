export default {
  id: 'read-the-fine-print', name: 'Read the Fine Print', time: 1793222400, tag: 'Studio',
  summary: 'The script editor now tells you how to use a function while you type it, and right / middle mouse buttons can be bound.',
  items: [
    'Autocomplete shows how to use the picked function: what to put in the ( ), what it gives back, and one line on what it does (like getmouse3d(distance?, ignore?) -> Vector3, BasePart?, Vector3)',
    'While you type inside a function\'s ( ), a hint above the cursor shows how to use it, with the part you\'re typing lit up',
    'Typed a name with the wrong upper / lower case, like getMouse3D? Autocomplete offers the right spelling, and the error says "did you mean getmouse3d?"',
    'ContextActionService can bind the right and middle mouse buttons (Enum.UserInputType.MouseButton2 / MouseButton3), and UserInputService hears them too',
    'Binding MouseButton1Click (a button\'s event) now just means the left mouse button instead of a warning',
    'Fixed the Insert Object window listing RemoteEvent, RemoteFunction and a few others twice, which made a "conflicting ID" error pop up',
  ],
};
