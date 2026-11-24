export default {
  id: 'missing-pieces', name: 'Missing Pieces', time: 1795506000, tag: 'Engine',
  summary: 'About 120 Roblox properties and methods scripts use every day now work, from Model:MoveTo to Lighting.TimeOfDay.',
  items: [
    'Models: MoveTo, GetBoundingBox, GetExtentsSize, TranslateBy, ScaleTo / GetScale, WorldPivot and the old SetPrimaryPartCFrame',
    'Parts: Resize, GetConnectedParts, GetJoints, GetVelocityAtPosition, ApplyImpulseAtPosition, CanQuery, CanTouch, Massless and SetNetworkOwner',
    'Attachments: WorldCFrame, WorldAxis, WorldOrientation, Axis and SecondaryAxis',
    'Lighting: TimeOfDay, GetMinutesAfterMidnight / SetMinutesAfterMidnight, GetSunDirection, GetMoonDirection, OutdoorAmbient and FogEnd',
    'RunService:BindToRenderStep / UnbindFromRenderStep and the new PreRender / PreSimulation / PostSimulation events',
    'UserInputService: GetKeysPressed, IsMouseButtonPressed, GetMouseDelta, GetLastInputType and more',
    'TextLabel.TextBounds and TextFits, TextService:GetTextBoundsAsync, and the image and layout settings (ImageRectOffset, ScaleType, AutomaticSize...) keep what scripts set',
    'Camera: GetPartsObscuringTarget, Interpolate, ZoomToExtents and DiagonalFieldOfView; TweenService:SmoothDamp; workspace.FallenPartsDestroyHeight and BulkMoveTo',
  ],
};
