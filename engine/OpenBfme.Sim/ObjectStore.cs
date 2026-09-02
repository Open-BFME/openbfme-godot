namespace OpenBfme.Sim;

/// <summary>
/// Dense-by-slot structure-of-arrays for renderer-facing object state. Slot zero
/// is permanently reserved. GameObject continues to own template/module state;
/// SimWorld mirrors its transform, ownership, flags, and body health here.
/// </summary>
public sealed class ObjectStore
{
    private const int MinimumCapacity = 4;
    private readonly Stack<int> _freeSlots = new();
    private int _nextSlot = 1;

    public ObjectStore(int initialCapacity = MinimumCapacity)
    {
        if (initialCapacity < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(initialCapacity));
        }
        var capacity = MinimumCapacity;
        while (capacity < initialCapacity)
        {
            capacity = checked(capacity * 2);
        }
        Id = new int[capacity];
        TemplateIndex = new int[capacity];
        Owner = new int[capacity];
        X = new Fixed64[capacity];
        Y = new Fixed64[capacity];
        Z = new Fixed64[capacity];
        Yaw = new Fixed64[capacity];
        Health = new Fixed64[capacity];
        MaxHealth = new Fixed64[capacity];
        State = new int[capacity];
        Anim = new int[capacity];
        AnimFrame = new Fixed64[capacity];
        Flags = new int[capacity];
    }

    public int[] Id { get; private set; }
    public int[] TemplateIndex { get; private set; }
    public int[] Owner { get; private set; }
    public Fixed64[] X { get; private set; }
    public Fixed64[] Y { get; private set; }
    public Fixed64[] Z { get; private set; }
    public Fixed64[] Yaw { get; private set; }
    public Fixed64[] Health { get; private set; }
    public Fixed64[] MaxHealth { get; private set; }
    public int[] State { get; private set; }
    public int[] Anim { get; private set; }
    public Fixed64[] AnimFrame { get; private set; }
    public int[] Flags { get; private set; }

    public int Count { get; private set; }
    public int Capacity => Id.Length;

    public int Allocate(int objectId)
    {
        if (objectId < 1)
        {
            throw new ArgumentOutOfRangeException(nameof(objectId), "Object id must be positive");
        }
        var slot = _freeSlots.Count > 0 ? _freeSlots.Pop() : AllocateNewSlot();
        Id[slot] = objectId;
        Owner[slot] = -1;
        Count++;
        return slot;
    }

    public void Free(int slot)
    {
        ValidateLiveSlot(slot);
        Id[slot] = 0;
        TemplateIndex[slot] = 0;
        Owner[slot] = 0;
        X[slot] = Fixed64.Zero;
        Y[slot] = Fixed64.Zero;
        Z[slot] = Fixed64.Zero;
        Yaw[slot] = Fixed64.Zero;
        Health[slot] = Fixed64.Zero;
        MaxHealth[slot] = Fixed64.Zero;
        State[slot] = 0;
        Anim[slot] = 0;
        AnimFrame[slot] = Fixed64.Zero;
        Flags[slot] = 0;
        _freeSlots.Push(slot);
        Count--;
    }

    public IEnumerable<int> LiveSlots()
    {
        for (var slot = 1; slot < _nextSlot; slot++)
        {
            if (Id[slot] != 0)
            {
                yield return slot;
            }
        }
    }

    public bool IsLive(int slot) => slot > 0 && slot < _nextSlot && Id[slot] != 0;

    private int AllocateNewSlot()
    {
        if (_nextSlot == Capacity)
        {
            Grow();
        }
        return _nextSlot++;
    }

    private void Grow()
    {
        var capacity = checked(Capacity * 2);
        Id = Resize(Id, capacity);
        TemplateIndex = Resize(TemplateIndex, capacity);
        Owner = Resize(Owner, capacity);
        X = Resize(X, capacity);
        Y = Resize(Y, capacity);
        Z = Resize(Z, capacity);
        Yaw = Resize(Yaw, capacity);
        Health = Resize(Health, capacity);
        MaxHealth = Resize(MaxHealth, capacity);
        State = Resize(State, capacity);
        Anim = Resize(Anim, capacity);
        AnimFrame = Resize(AnimFrame, capacity);
        Flags = Resize(Flags, capacity);
    }

    private static T[] Resize<T>(T[] source, int capacity)
    {
        Array.Resize(ref source, capacity);
        return source;
    }

    private void ValidateLiveSlot(int slot)
    {
        if (!IsLive(slot))
        {
            throw new ArgumentOutOfRangeException(nameof(slot), $"Slot {slot} is not live");
        }
    }
}
